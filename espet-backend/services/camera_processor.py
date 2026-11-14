from __future__ import annotations

import asyncio
import time
from dataclasses import dataclass, field

from config import settings
from services.owner_face_service import owner_face_service


@dataclass
class CameraStreamState:
    device_id: str
    latest_raw_jpeg: bytes | None = None
    latest_processed_jpeg: bytes | None = None
    latest_detections: list[dict] = field(default_factory=list)
    fps: float = 0.0
    target_fps: int = 0
    frame_ts: float = 0.0
    inference_ms: float = 0.0
    frame_count: int = 0
    viewer_count: int = 0
    streaming_requested: bool = False
    mode: str = "processed"
    cv_backend: str = "none"
    owner_detected: bool = False
    owner_last_seen_ts: float = 0.0
    owner_similarity: float = 0.0
    owner_face_error: str | None = None
    owner_face_enrolled: bool = False
    frame_event: asyncio.Condition = field(default_factory=asyncio.Condition)

    _last_frame_ts: float = 0.0
    _last_face_check_ts: float = 0.0
    _face_task: asyncio.Task | None = None

    async def update_frame(self, jpeg: bytes) -> None:
        now = time.time()
        if self._last_frame_ts:
            delta = max(now - self._last_frame_ts, 0.001)
            sample = 1.0 / delta
            self.fps = sample if self.fps <= 0 else (self.fps * 0.8 + sample * 0.2)
        self._last_frame_ts = now
        self.frame_ts = now
        self.frame_count += 1
        self.latest_raw_jpeg = jpeg
        self.latest_processed_jpeg = jpeg
        if self._should_check_faces(now):
            self._last_face_check_ts = now
            self._face_task = asyncio.create_task(self._detect_faces(jpeg))
        async with self.frame_event:
            self.frame_event.notify_all()

    def _should_check_faces(self, now: float) -> bool:
        if self._face_task and not self._face_task.done():
            return False
        if now - self._last_face_check_ts < settings.camera_face_interval_s:
            return False
        status = owner_face_service.status(self.device_id)
        self.owner_face_enrolled = bool(status.get("enrolled"))
        self.owner_face_error = status.get("error")
        return self.owner_face_enrolled and self.owner_face_error is None

    async def _detect_faces(self, jpeg: bytes) -> None:
        try:
            result = await asyncio.to_thread(owner_face_service.detect_owner, self.device_id, jpeg)
            self.latest_detections = result.get("detections", [])
            self.inference_ms = float(result.get("inference_ms") or 0.0)
            self.owner_face_enrolled = bool(result.get("enrolled"))
            self.owner_face_error = result.get("error")
            self.owner_similarity = float(result.get("best_similarity") or 0.0)
            self.owner_detected = bool(result.get("owner_detected"))
            if self.owner_detected:
                self.owner_last_seen_ts = time.time()
        finally:
            async with self.frame_event:
                self.frame_event.notify_all()

    def snapshot(self, mode: str = "processed") -> bytes | None:
        if mode == "raw":
            return self.latest_raw_jpeg
        return self.latest_processed_jpeg or self.latest_raw_jpeg

    def status(self) -> dict:
        last_frame_age_ms = 0.0
        if self.frame_ts:
            last_frame_age_ms = max(0.0, (time.time() - self.frame_ts) * 1000.0)
        return {
            "device_id": self.device_id,
            "streaming": self.streaming_requested,
            "fps": round(self.fps, 2),
            "target_fps": self.target_fps,
            "viewer_count": self.viewer_count,
            "frame_count": self.frame_count,
            "frame_ts": self.frame_ts,
            "last_frame_age_ms": round(last_frame_age_ms, 0),
            "inference_ms": round(self.inference_ms, 2),
            "detections": self.latest_detections,
            "cv_backend": self.cv_backend,
            "mode": self.mode,
            "has_frame": self.latest_raw_jpeg is not None,
            "owner_detected": self.owner_detected,
            "owner_last_seen_ts": self.owner_last_seen_ts,
            "owner_similarity": round(self.owner_similarity, 3),
            "owner_face_enrolled": self.owner_face_enrolled,
            "owner_face_error": self.owner_face_error,
        }


class CameraProcessor:
    def __init__(self) -> None:
        self._states: dict[str, CameraStreamState] = {}
        self._lock = asyncio.Lock()

    async def state(self, device_id: str) -> CameraStreamState:
        did = device_id or "espet_s3_01"
        async with self._lock:
            if did not in self._states:
                backend = settings.camera_cv_backend if settings.camera_cv_enabled else "none"
                self._states[did] = CameraStreamState(device_id=did, cv_backend=backend)
            return self._states[did]

    async def update_frame(self, device_id: str, jpeg: bytes) -> CameraStreamState:
        st = await self.state(device_id)
        await st.update_frame(jpeg)
        return st

    async def all_status(self) -> list[dict]:
        async with self._lock:
            return [st.status() for st in self._states.values()]


camera_processor = CameraProcessor()
