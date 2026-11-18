from __future__ import annotations

import asyncio
import base64
import json
import logging
import re
import time
from typing import AsyncIterator

import httpx
from fastapi import (
    APIRouter,
    Depends,
    File,
    Header,
    HTTPException,
    Query,
    Request,
    Response,
    UploadFile,
    WebSocket,
    WebSocketDisconnect,
)
from fastapi.responses import StreamingResponse
from pathlib import Path

from auth.device_auth import verify_device_key
from config import settings
from services.camera_processor import camera_processor, set_owner_welcome_callback
from services.gateway_settings import gateway_settings
from services.owner_face_service import owner_face_service
from services.telegram_gateway import get_gateway
from api.cloud_router import registry, record_new_photo

logger = logging.getLogger("espet-backend.camera")
router = APIRouter(prefix="/api/camera", tags=["camera"])
_owner_watch_task: asyncio.Task | None = None
LAST_PHOTO_PATH = Path(__file__).parent.parent / "data" / "last_photo.jpg"


def _clean_oled_line(s: str) -> str:
    """Ensure string is strictly ASCII and at most 16 characters."""
    if not s:
        return ""
    # Normalize and drop non-ASCII (OLED SSD1306 font is ASCII only)
    ascii_str = s.encode("ascii", "ignore").decode("ascii")
    clean = ascii_str.replace("\r", " ").replace("\n", " ").strip()
    return clean[:16].strip()


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


async def _send_tts_say(device_id: str, text: str) -> bool:
    return await _send_camera_control(device_id, {
        "type": "tts_say",
        "text": text,
        "display_title": "Owner Detected",
        "display_body": "Welcome Back",
    })


set_owner_welcome_callback(_send_tts_say)


async def _owner_watch_loop() -> None:
    logger.info("Owner camera watch loop started")
    try:
        while True:
            interval_s = max(10, int(settings.camera_owner_watch_interval_s))
            if not settings.camera_owner_watch_enabled:
                await asyncio.sleep(interval_s)
                continue

            devices = registry.connected_devices()
            for device_id in devices:
                face_status = owner_face_service.status(device_id)
                if not face_status.get("enrolled") or face_status.get("error"):
                    continue
                st = await camera_processor.state(device_id)
                if st.viewer_count > 0:
                    continue

                fps = max(1, min(int(settings.camera_owner_watch_fps), 5))
                sample_s = max(2, min(int(settings.camera_owner_watch_sample_s), 15))
                pushed = await _request_start(device_id, fps)
                if not pushed:
                    continue
                logger.info("Owner watch sampling %s for %ds at %d fps", device_id, sample_s, fps)
                await asyncio.sleep(sample_s)
                st = await camera_processor.state(device_id)
                if st.viewer_count == 0:
                    st.streaming_requested = False
                    await _send_camera_control(device_id, {"type": "camera_stream_stop"})

            await asyncio.sleep(interval_s)
    except asyncio.CancelledError:
        logger.info("Owner camera watch loop stopped")
        raise


def start_owner_watch_loop() -> None:
    global _owner_watch_task
    if _owner_watch_task and not _owner_watch_task.done():
        return
    _owner_watch_task = asyncio.create_task(_owner_watch_loop())


async def stop_owner_watch_loop() -> None:
    global _owner_watch_task
    if not _owner_watch_task:
        return
    _owner_watch_task.cancel()
    try:
        await _owner_watch_task
    except asyncio.CancelledError:
        pass
    _owner_watch_task = None


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


@router.get("/last-photo")
async def get_last_photo(t: str = Query("")):
    """Return the most recently captured photo (JPEG)."""
    if not LAST_PHOTO_PATH.exists():
        raise HTTPException(status_code=404, detail="No photo captured yet")
    return Response(
        content=LAST_PHOTO_PATH.read_bytes(),
        media_type="image/jpeg",
        headers={
            "Cache-Control": "no-cache, no-store, must-revalidate",
            "Pragma": "no-cache",
            "Expires": "0",
        },
    )


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
        return {"ok": True, **owner_face_service.enroll(device_id, data, file.content_type or "image/jpeg")}
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


@router.post("/owner-face/test-welcome")
async def test_owner_welcome(payload: dict | None = None, device_id: str | None = Query(None), _key: str = Depends(verify_device_key)):
    data = payload or {}
    did = str(data.get("device_id") or device_id or "espet_s3_01")
    return {
        "ok": False,
        "device_id": did,
        "pushed": False,
        "error": "direct_welcome_test_disabled_use_test_detect",
    }


@router.post("/owner-face/test-detect")
async def test_owner_detection(payload: dict | None = None, device_id: str | None = Query(None), _key: str = Depends(verify_device_key)):
    data = payload or {}
    did = str(data.get("device_id") or device_id or "espet_s3_01")
    face_status = owner_face_service.status(did)
    if not face_status.get("enrolled"):
        return {"ok": False, "device_id": did, "detected": False, "action_sent": False, "error": "owner_face_not_enrolled"}
    if face_status.get("error"):
        return {"ok": False, "device_id": did, "detected": False, "action_sent": False, "error": face_status.get("error")}

    sample_s = max(2, min(int(data.get("sample_s") or settings.camera_owner_watch_sample_s), 15))
    fps = max(1, min(int(data.get("fps") or settings.camera_owner_watch_fps), 5))
    start_ts = time.time()
    pushed = await _request_start(did, fps)
    if not pushed:
        return {"ok": False, "device_id": did, "detected": False, "action_sent": False, "error": "device_offline"}

    detected = False
    deadline = time.time() + sample_s
    st = await camera_processor.state(did)
    while time.time() < deadline:
        st = await camera_processor.state(did)
        if st.owner_detected and st.owner_last_seen_ts >= start_ts:
            detected = True
            break
        async with st.frame_event:
            try:
                await asyncio.wait_for(st.frame_event.wait(), timeout=0.5)
            except asyncio.TimeoutError:
                pass

    action_sent = False
    if detected:
        if st.owner_welcome_last_ts >= start_ts:
            action_sent = True
        else:
            text = str(data.get("text") or settings.camera_owner_welcome_text or "Hi owner, welcome back.")
            action_sent = await _send_tts_say(did, text)
            if action_sent:
                st.owner_welcome_last_ts = time.time()

    st = await camera_processor.state(did)
    if st.viewer_count == 0:
        st.streaming_requested = False
        await _send_camera_control(did, {"type": "camera_stream_stop"})

    return {
        "ok": detected and action_sent,
        "device_id": did,
        "detected": detected,
        "action_sent": action_sent,
        "sample_s": sample_s,
        "fps": fps,
        "owner_similarity": st.owner_similarity,
        "owner_last_seen_ts": st.owner_last_seen_ts,
    }


@router.get("/owner-face/preview")
async def owner_face_preview(device_id: str = Query("espet_s3_01"), key: str = Query("")):
    _check_query_key(key)
    preview = owner_face_service.preview(device_id)
    if not preview:
        raise HTTPException(status_code=404, detail="Owner face preview not found")
    data, content_type = preview
    return Response(content=data, media_type=content_type)


@router.post("/vision-query")
async def vision_query(
    request: Request,
    query: str = Query(""),
    device_id: str = Header("", alias="X-Device-Id"),
    _key: str = Depends(verify_device_key),
):
    """
    Multimodal Vision Query from ESP32:
    - Body: raw binary JPEG captured by OV2640 camera
    - Query: optional voice transcription text (or passed via X-Query-B64 header)
    - Device ID: ESP32 hardware device identifier
    """
    image_bytes = await request.body()
    if not image_bytes:
        raise HTTPException(status_code=400, detail="Empty image body")

    # Persist the latest captured photo for web chat display
    try:
        LAST_PHOTO_PATH.parent.mkdir(parents=True, exist_ok=True)
        LAST_PHOTO_PATH.write_bytes(image_bytes)
        record_new_photo()
    except Exception as e:
        logger.warning("Failed to save last_photo.jpg: %r", e)

    # Check X-Query-B64 header if query parameter is empty
    b64_query = request.headers.get("x-query-b64", "").strip()
    if b64_query and not query:
        try:
            query = base64.b64decode(b64_query).decode("utf-8", errors="ignore").strip()
        except Exception:
            pass

    clean_query = (query or "").strip()

    # Resolve bound Telegram chat
    chat_id = None
    if device_id:
        chat_id = registry.primary_chat_for_device(device_id)
    if not chat_id and registry.chat_to_device:
        chat_id = next(iter(registry.chat_to_device.keys()))

    gw = get_gateway()

    # -------------------------------------------------------------
    # 1. Rule-based / Snapshot Branch: Capture only or explicit snapshot
    # -------------------------------------------------------------
    if not clean_query or clean_query.startswith("📸") or "snapshot" in clean_query.lower():
        rule_reply = "Photo captured successfully."
        rule_caption = f"📸 [Photo Captured]\n{rule_reply}"
        if gw and chat_id and chat_id != "dashboard":
            try:
                await gw.send_photo(chat_id, image_bytes, caption=rule_caption)
                logger.info("Sent rule-based photo to Telegram chat %s", chat_id)
            except Exception as e:
                logger.warning("Failed to send rule-based photo to Telegram: %r", e)
        return {
            "ok": True,
            "rule_based": True,
            "reply": rule_reply,
            "oled_l1": "Photo Captured",
            "oled_l2": "Sent to Telegram",
            "image_url": f"/api/camera/last-photo?t={int(time.time() * 1000)}",
        }

    # -------------------------------------------------------------
    # 2. Capture & Ask Branch: Photo + User Voice Question
    # -------------------------------------------------------------
    user_caption = f"👤 You asked:\n\"{clean_query}\""
    if gw and chat_id and chat_id != "dashboard":
        try:
            await gw.send_photo(chat_id, image_bytes, caption=user_caption)
            logger.info("Sent user question photo to Telegram chat %s: %s", chat_id, clean_query)
        except Exception as e:
            logger.warning("Failed to send question photo to Telegram: %r", e)

    # Call Vision LLM
    cfg = gateway_settings.llm_config()
    snap = gateway_settings.get()
    vision_model = snap.get("vision_model") or "sensenova/sensenova-6.8-flash-lite"
    api_key = cfg.get("api_key")
    base_url = (cfg.get("base_url") or "https://api.xkiro.com/v1").rstrip("/")

    img_b64 = base64.b64encode(image_bytes).decode("ascii")
    payload = {
        "model": vision_model,
        "messages": [
            {
                "role": "system",
                "content": (
                    "You are ESPET, a helpful and friendly embodied personal AI companion equipped with a camera and a 128x64 physical OLED screen. "
                    "Analyze the user's camera image and question, then return a valid JSON object with EXACTLY two fields:\n"
                    "1. \"oled\": an array of EXACTLY two short strings (strictly <= 16 ASCII characters per line) summarizing what you see/answer. "
                    "IMPORTANT: The OLED screen font is ASCII-only (no Chinese font), so the OLED strings must ALWAYS be in English or Romanized Pinyin.\n"
                    "2. \"reply\": your full conversational answer (1 to 3 concise sentences) in the user's spoken language, suitable for text-to-speech speaker and Telegram chat.\n\n"
                    "Example JSON:\n"
                    "{\"oled\": [\"Seeing pale wall\", \"Hand in front\"], \"reply\": \"I see a pale wall with what looks like your hand or phone in the foreground.\"} \n\n"
                    "Output ONLY valid raw JSON without markdown code fences."
                ),
            },
            {
                "role": "user",
                "content": [
                    {"type": "text", "text": clean_query},
                    {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{img_b64}"}},
                ],
            },
        ],
        "max_tokens": 512,
    }

    ai_reply = "I'm sorry, I could not analyze the image right now."
    oled_l1 = ""
    oled_l2 = ""

    try:
        async with httpx.AsyncClient(timeout=30.0) as client:
            resp = await client.post(
                f"{base_url}/chat/completions",
                json=payload,
                headers={
                    "Authorization": f"Bearer {api_key}",
                    "Content-Type": "application/json",
                    "User-Agent": "ESPET-Backend/1.0",
                },
            )
            if resp.status_code == 200:
                data = resp.json()
                choices = data.get("choices", [])
                if choices and "message" in choices[0]:
                    raw_content = choices[0]["message"].get("content", "").strip()
                    logger.info("Vision LLM (%s) raw content: %s", vision_model, raw_content)

                    # Strip markdown codeblocks if model included them
                    cleaned_json = raw_content
                    if cleaned_json.startswith("```"):
                        lines = cleaned_json.splitlines()
                        if len(lines) >= 2 and lines[0].startswith("```"):
                            cleaned_json = "\n".join(lines[1:-1] if lines[-1].startswith("```") else lines[1:])
                        cleaned_json = cleaned_json.strip()

                    parsed = None
                    try:
                        parsed = json.loads(cleaned_json)
                    except Exception:
                        # Try finding { ... } substring
                        match = re.search(r"\{.*\}", cleaned_json, re.DOTALL)
                        if match:
                            try:
                                parsed = json.loads(match.group(0))
                            except Exception:
                                pass

                    if isinstance(parsed, dict):
                        if "reply" in parsed and str(parsed["reply"]).strip():
                            ai_reply = str(parsed["reply"]).strip()
                        oled_items = parsed.get("oled")
                        if isinstance(oled_items, list):
                            if len(oled_items) > 0:
                                oled_l1 = _clean_oled_line(str(oled_items[0]))
                            if len(oled_items) > 1:
                                oled_l2 = _clean_oled_line(str(oled_items[1]))
                    else:
                        ai_reply = raw_content
            else:
                logger.error("Vision LLM failed: HTTP %d %s", resp.status_code, resp.text)
    except Exception as exc:
        logger.error("Vision LLM call exception: %r", exc)

    # Fallback for OLED lines if model did not supply valid ones
    if not oled_l1 and not oled_l2:
        oled_l1 = _clean_oled_line(clean_query or "Vision Query")
        oled_l2 = _clean_oled_line(ai_reply)

    # Send AI reply to Telegram (Clean standard text format: '🤖 ESPET: ...')
    if gw and chat_id and chat_id != "dashboard":
        try:
            await gw.send_text(chat_id, f"🤖 ESPET: {ai_reply}")
            logger.info("Sent AI reply to Telegram chat %s: %.60s", chat_id, ai_reply)
        except Exception as e:
            logger.warning("Failed to send AI reply to Telegram: %r", e)

    # Also broadcast to Web Chat if connected
    try:
        from api.cloud_router import _broadcast_to_web_chat
        await _broadcast_to_web_chat(chat_id or "dashboard", f"🤖 ESPET: {ai_reply}")
    except Exception as e:
        logger.debug("Web chat broadcast skipped: %r", e)

    return {
        "ok": True,
        "rule_based": False,
        "reply": ai_reply,
        "oled_l1": oled_l1,
        "oled_l2": oled_l2,
    }
