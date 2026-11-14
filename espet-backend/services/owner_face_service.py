from __future__ import annotations

import hashlib
import importlib.util
import json
import time
from pathlib import Path
from threading import Lock

from config import settings


class OwnerFaceService:
    def __init__(self) -> None:
        self._app = None
        self._model_error: str | None = None
        self._lock = Lock()
        self.data_dir = Path("data/owner_faces")

    def _safe_device_id(self, device_id: str) -> str:
        return "".join(ch if ch.isalnum() or ch in ("-", "_") else "_" for ch in (device_id or "espet_s3_01"))

    def _path(self, device_id: str) -> Path:
        return self.data_dir / f"{self._safe_device_id(device_id)}.json"

    def _preview_path(self, device_id: str) -> Path:
        return self.data_dir / f"{self._safe_device_id(device_id)}.preview"

    def _ensure_model(self) -> None:
        if self._app is not None:
            return
        if self._model_error:
            raise RuntimeError(self._model_error)
        try:
            from insightface.app import FaceAnalysis
        except Exception as exc:
            self._model_error = (
                "Owner face model not installed. Run: pip install insightface onnxruntime opencv-python-headless"
            )
            raise RuntimeError(self._model_error) from exc
        try:
            app = FaceAnalysis(name="buffalo_l", providers=["CPUExecutionProvider"])
            app.prepare(ctx_id=-1, det_size=(320, 320))
            self._app = app
        except Exception as exc:
            self._model_error = f"Owner face model failed to initialize: {type(exc).__name__}: {exc}"
            raise RuntimeError(self._model_error) from exc

    def _decode(self, image_bytes: bytes):
        try:
            import cv2
            import numpy as np
        except Exception as exc:
            raise RuntimeError("OpenCV/numpy not installed. Run: pip install opencv-python-headless numpy") from exc
        arr = np.frombuffer(image_bytes, dtype=np.uint8)
        img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
        if img is None:
            raise ValueError("Image is not a readable JPEG/PNG")
        return img

    def _faces(self, image_bytes: bytes):
        self._ensure_model()
        img = self._decode(image_bytes)
        return self._app.get(img)

    def _load(self, device_id: str) -> dict | None:
        path = self._path(device_id)
        if not path.exists():
            return None
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            return None

    def _dependency_error(self) -> str | None:
        missing = [name for name in ("insightface", "onnxruntime", "cv2") if importlib.util.find_spec(name) is None]
        if missing:
            return (
                "Owner face model not installed. Run: pip install insightface onnxruntime opencv-python-headless"
            )
        return self._model_error

    def status(self, device_id: str) -> dict:
        enrolled = self._load(device_id)
        dep_error = self._dependency_error()
        return {
            "device_id": device_id,
            "enabled": dep_error is None,
            "installed": dep_error is None,
            "enrolled": enrolled is not None,
            "enrolled_at": enrolled.get("enrolled_at") if enrolled else None,
            "image_sha256": enrolled.get("image_sha256") if enrolled else None,
            "has_preview": self._preview_path(device_id).exists(),
            "threshold": settings.camera_owner_match_threshold,
            "watch_enabled": settings.camera_owner_watch_enabled,
            "watch_interval_s": settings.camera_owner_watch_interval_s,
            "watch_sample_s": settings.camera_owner_watch_sample_s,
            "watch_fps": settings.camera_owner_watch_fps,
            "error": dep_error,
        }

    def enroll(self, device_id: str, image_bytes: bytes, content_type: str = "image/jpeg") -> dict:
        with self._lock:
            faces = self._faces(image_bytes)
            if len(faces) != 1:
                raise ValueError(f"Owner photo must contain exactly one face; found {len(faces)}")
            face = faces[0]
            embedding = getattr(face, "normed_embedding", None)
            if embedding is None:
                raise RuntimeError("Face embedding unavailable")
            payload = {
                "device_id": device_id,
                "enrolled_at": time.time(),
                "image_sha256": hashlib.sha256(image_bytes).hexdigest(),
                "content_type": content_type if content_type.startswith("image/") else "image/jpeg",
                "embedding": [float(x) for x in embedding.tolist()],
            }
            self.data_dir.mkdir(parents=True, exist_ok=True)
            tmp = self._path(device_id).with_suffix(".tmp")
            tmp.write_text(json.dumps(payload), encoding="utf-8")
            tmp.replace(self._path(device_id))
            preview_tmp = self._preview_path(device_id).with_suffix(".tmp")
            preview_tmp.write_bytes(image_bytes)
            preview_tmp.replace(self._preview_path(device_id))
            return self.status(device_id)

    def delete(self, device_id: str) -> bool:
        path = self._path(device_id)
        preview_path = self._preview_path(device_id)
        deleted = False
        if path.exists():
            path.unlink()
            deleted = True
        if preview_path.exists():
            preview_path.unlink()
            deleted = True
        return deleted

    def preview(self, device_id: str) -> tuple[bytes, str] | None:
        path = self._preview_path(device_id)
        enrolled = self._load(device_id)
        if not path.exists() or not enrolled:
            return None
        return path.read_bytes(), str(enrolled.get("content_type") or "image/jpeg")

    def detect_owner(self, device_id: str, image_bytes: bytes) -> dict:
        start = time.time()
        enrolled = self._load(device_id)
        if not enrolled:
            return {
                "available": self._model_error is None,
                "enrolled": False,
                "owner_detected": False,
                "detections": [],
                "inference_ms": 0.0,
                "error": self._model_error,
            }

        try:
            import numpy as np
            faces = self._faces(image_bytes)
            owner = np.array(enrolled["embedding"], dtype="float32")
            detections: list[dict] = []
            owner_detected = False
            best_similarity = 0.0
            for face in faces:
                embedding = getattr(face, "normed_embedding", None)
                if embedding is None:
                    continue
                similarity = float(np.dot(owner, embedding))
                best_similarity = max(best_similarity, similarity)
                matched = similarity >= settings.camera_owner_match_threshold
                if matched:
                    owner_detected = True
                bbox = [int(v) for v in getattr(face, "bbox", [0, 0, 0, 0]).tolist()]
                detections.append({
                    "label": "Owner" if matched else "Unknown Face",
                    "confidence": round(similarity, 3),
                    "bbox": bbox,
                    "owner_match": matched,
                })
            return {
                "available": True,
                "enrolled": True,
                "owner_detected": owner_detected,
                "best_similarity": round(best_similarity, 3),
                "detections": detections,
                "inference_ms": round((time.time() - start) * 1000.0, 2),
                "error": None,
            }
        except Exception as exc:
            return {
                "available": False,
                "enrolled": True,
                "owner_detected": False,
                "best_similarity": 0.0,
                "detections": [],
                "inference_ms": round((time.time() - start) * 1000.0, 2),
                "error": f"{type(exc).__name__}: {exc}",
            }


owner_face_service = OwnerFaceService()
