from __future__ import annotations

import asyncio
import json
import logging
from typing import AsyncIterator

from fastapi import APIRouter, Depends, File, HTTPException, Query, Response, UploadFile, WebSocket, WebSocketDisconnect
from fastapi.responses import StreamingResponse

from auth.device_auth import verify_device_key
from config import settings
from services.camera_processor import camera_processor
from services.owner_face_service import owner_face_service
from api.cloud_router import registry

logger = logging.getLogger("espet-backend.camera")
router = APIRouter(prefix="/api/camera", tags=["camera"])


def _check_query_key(key: str) -> None:
    if not settings.device_api_key or key != settings.device_api_key:
        raise HTTPException(status_code=401, detail="Invalid device key")


async def _send_camera_control(device_id: str, frame: dict) -> bool:
    did, ws = registry.get_connection(device_id)
    if not did or not ws:
        return False
    try:
        await ws.send_text(json.dumps(frame))
        return True
    except Exception as exc:
        logger.warning("Camera control push to %s failed: %r", did, exc)
        return False


async def _request_start(device_id: str, fps: int) -> bool:
    st = await camera_processor.state(device_id)
    st.streaming_requested = True
    st.target_fps = fps
    return await _send_camera_control(device_id, {"type": "camera_stream_start", "fps": fps})


async def _request_stop_after_grace(device_id: str, grace_s: float = 2.0) -> None:
    await asyncio.sleep(grace_s)
    st = await camera_processor.state(device_id)
    if st.viewer_count <= 0:
        st.streaming_requested = False
        await _send_camera_control(device_id, {"type": "camera_stream_stop"})


@router.websocket("/ws/device")
async def camera_ws_device(websocket: WebSocket, key: str = Query(""), device_id: str = Query("espet_s3_01")):
    if not settings.device_api_key or key != settings.device_api_key:
        await websocket.accept()
        await websocket.close(code=4401)
        return
    await websocket.accept()
    logger.info("Camera device stream connected: %s", device_id)
    try:
        while True:
            msg = await websocket.receive()
            data = msg.get("bytes")
            if data:
                await camera_processor.update_frame(device_id, data)
            elif msg.get("type") == "websocket.disconnect":
                break
    except WebSocketDisconnect:
        pass
    finally:
        logger.info("Camera device stream disconnected: %s", device_id)


async def _mjpeg_frames(device_id: str, mode: str, fps: int) -> AsyncIterator[bytes]:
    st = await camera_processor.state(device_id)
    first_viewer = st.viewer_count == 0
    st.viewer_count += 1
    if first_viewer:
        await _request_start(device_id, fps)

    last_count = -1
    try:
        while True:
            timed_out = False
            async with st.frame_event:
                try:
                    await asyncio.wait_for(
                        st.frame_event.wait_for(lambda: st.frame_count != last_count),
                        timeout=3.0,
                    )
                    last_count = st.frame_count
                except asyncio.TimeoutError:
                    timed_out = True
            if timed_out:
                logger.warning("Camera stream stale for %s; nudging device start", device_id)
                await _request_start(device_id, fps)
                continue
            frame = st.snapshot(mode)
            if not frame:
                continue
            yield (
                b"--frame\r\n"
                b"Content-Type: image/jpeg\r\n"
                + f"Content-Length: {len(frame)}\r\n\r\n".encode("ascii")
                + frame
                + b"\r\n"
            )
    finally:
        st.viewer_count = max(0, st.viewer_count - 1)
        if st.viewer_count == 0:
            asyncio.create_task(_request_stop_after_grace(device_id))


@router.get("/stream")
async def stream_camera(
    device_id: str = Query("espet_s3_01"),
    mode: str = Query("processed"),
    fps: int = Query(settings.camera_default_fps),
    key: str = Query(""),
):
    _check_query_key(key)
    fps = max(1, min(int(fps), 15))
    mode = "raw" if mode == "raw" else "processed"
    return StreamingResponse(
        _mjpeg_frames(device_id, mode, fps),
        media_type="multipart/x-mixed-replace; boundary=frame",
    )


@router.get("/snapshot")
async def snapshot(device_id: str = Query("espet_s3_01"), mode: str = Query("processed"), key: str = Query("")):
    _check_query_key(key)
    st = await camera_processor.state(device_id)
    frame = st.snapshot("raw" if mode == "raw" else "processed")
    if not frame:
        raise HTTPException(status_code=404, detail="No camera frame available")
    return Response(content=frame, media_type="image/jpeg")


@router.get("/status")
async def status(device_id: str = Query("espet_s3_01"), _key: str = Depends(verify_device_key)):
    st = await camera_processor.state(device_id)
    return st.status()


@router.get("/status/all")
async def status_all(_key: str = Depends(verify_device_key)):
    return {"devices": await camera_processor.all_status()}


@router.post("/stream/start")
async def start_stream(payload: dict | None = None, device_id: str | None = Query(None), _key: str = Depends(verify_device_key)):
    data = payload or {}
    did = str(data.get("device_id") or device_id or "espet_s3_01")
    fps = max(1, min(int(data.get("fps") or settings.camera_default_fps), 15))
    pushed = await _request_start(did, fps)
    return {"ok": pushed, "device_id": did, "fps": fps, "pushed": pushed}


@router.post("/stream/stop")
async def stop_stream(payload: dict | None = None, device_id: str | None = Query(None), _key: str = Depends(verify_device_key)):
    data = payload or {}
    did = str(data.get("device_id") or device_id or "espet_s3_01")
    st = await camera_processor.state(did)
    st.streaming_requested = False
    pushed = await _send_camera_control(did, {"type": "camera_stream_stop"})
    return {"ok": pushed, "device_id": did, "pushed": pushed}


@router.put("/config")
async def camera_config(payload: dict, _key: str = Depends(verify_device_key)):
    did = str(payload.get("device_id") or "espet_s3_01")
    st = await camera_processor.state(did)
    if payload.get("mode") in ("raw", "processed"):
        st.mode = payload["mode"]
    if payload.get("cv_backend") in ("none", "opencv", "mediapipe", "yolo"):
        st.cv_backend = payload["cv_backend"]
    return {"ok": True, **st.status()}


@router.get("/owner-face/status")
async def owner_face_status(device_id: str = Query("espet_s3_01"), _key: str = Depends(verify_device_key)):
    return owner_face_service.status(device_id)


@router.post("/owner-face")
async def enroll_owner_face(
    device_id: str = Query("espet_s3_01"),
    file: UploadFile = File(...),
    _key: str = Depends(verify_device_key),
):
    data = await file.read()
    if not data:
        raise HTTPException(status_code=400, detail="Owner photo is empty")
    try:
        return {"ok": True, **owner_face_service.enroll(device_id, data)}
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    except RuntimeError as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc


@router.delete("/owner-face")
async def delete_owner_face(device_id: str = Query("espet_s3_01"), _key: str = Depends(verify_device_key)):
    deleted = owner_face_service.delete(device_id)
    st = await camera_processor.state(device_id)
    st.owner_detected = False
    st.owner_last_seen_ts = 0.0
    st.owner_similarity = 0.0
    st.owner_face_enrolled = False
    st.latest_detections = []
    return {"ok": True, "deleted": deleted, **owner_face_service.status(device_id)}
