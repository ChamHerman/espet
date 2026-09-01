"""
Configuration Router

Web dashboard + device config sync endpoints.

  GET    /api/config            — full config bundle (device pull + UI load)
  GET    /api/config/version    — per-section versions only (cheap poll)
  PUT    /api/ui/profile        — USER.md content
  PUT    /api/ui/soul           — SOUL.md content
  PUT    /api/ui/memory         — MEMORY.md content
  PUT    /api/ui/device         — device name / timezone / sync interval
  GET    /api/ui/jobs           — list web-managed cron jobs
  POST   /api/ui/jobs           — create a cron job
  PUT    /api/ui/jobs/{job_id}  — edit a cron job
  DELETE /api/ui/jobs/{job_id}  — delete a cron job

All endpoints require the shared device API key (Bearer), the same key the
ESP32 already uses for /api/mcp/execute. Single-user scope by design.
"""

import logging

from fastapi import APIRouter, Body, Depends, HTTPException

from auth.device_auth import verify_device_key
import config_store as store

router = APIRouter(prefix="/api", tags=["config"])
logger = logging.getLogger("espet-backend.api.config")


@router.get(
    "/config",
    summary="Full configuration bundle",
    description=(
        "Everything the device pulls: profile/personality/memory markdown, "
        "device settings (with POSIX TZ for the ESP32), and web-managed cron "
        "jobs. Per-section versions let the device apply only what changed."
    ),
)
async def get_config(_key: str = Depends(verify_device_key)):
    return store.device_bundle(store.get_config())


@router.get("/config/version", summary="Per-section config versions")
async def get_config_version(_key: str = Depends(verify_device_key)):
    cfg = store.get_config()
    return {"versions": cfg["versions"], "device": cfg["device"]}


@router.put("/ui/profile", summary="Update USER.md (profile)")
async def put_profile(body: dict = Body(...), _key: str = Depends(verify_device_key)):
    content = body.get("user_md")
    if content is None or not content.strip():
        raise HTTPException(422, "user_md is required")
    cfg = store.update_markdown("user", content)
    return {"ok": True, "versions": cfg["versions"]}


@router.put("/ui/soul", summary="Update SOUL.md (personality)")
async def put_soul(body: dict = Body(...), _key: str = Depends(verify_device_key)):
    content = body.get("soul_md")
    if content is None or not content.strip():
        raise HTTPException(422, "soul_md is required")
    cfg = store.update_markdown("soul", content)
    return {"ok": True, "versions": cfg["versions"]}


@router.put("/ui/memory", summary="Update MEMORY.md (long-term memory)")
async def put_memory(body: dict = Body(...), _key: str = Depends(verify_device_key)):
    content = body.get("memory_md")
    if content is None:
        raise HTTPException(422, "memory_md is required")
    cfg = store.update_markdown("memory", content)
    return {"ok": True, "versions": cfg["versions"]}


@router.put("/ui/device", summary="Update device settings")
async def put_device(body: dict = Body(...), _key: str = Depends(verify_device_key)):
    cfg = store.update_device_settings(
        name=body.get("name", ""),
        timezone_iana=body.get("timezone", ""),
        sync_interval_s=body.get("sync_interval_s", 0),
    )
    return {"ok": True, "versions": cfg["versions"], "device": cfg["device"]}


# ── Avatar (dashboard-only, not synced to the device) ─────────────

@router.get("/ui/avatar", summary="Agent avatar dress-up state")
async def get_avatar(_key: str = Depends(verify_device_key)):
    return store.get_avatar()


@router.put("/ui/avatar", summary="Update agent avatar")
async def put_avatar(body: dict = Body(...), _key: str = Depends(verify_device_key)):
    cfg, error = store.update_avatar(
        mode=body.get("mode", "auto"),
        opts=body.get("opts", {}),
    )
    if error:
        raise HTTPException(422, error)
    return {"ok": True, "versions": cfg["versions"], "avatar": cfg["avatar"]}


# ── Cron jobs ─────────────────────────────────────────────────────

@router.get("/ui/jobs", summary="List web-managed cron jobs")
async def list_jobs(_key: str = Depends(verify_device_key)):
    return {"jobs": store.get_config()["jobs"]}


@router.post("/ui/jobs", summary="Create a cron job")
async def create_job(body: dict = Body(...), _key: str = Depends(verify_device_key)):
    cfg, error = store.add_job(body)
    if error:
        raise HTTPException(422, error)
    return {"ok": True, "versions": cfg["versions"], "jobs": cfg["jobs"]}


@router.put("/ui/jobs/{job_id}", summary="Edit a cron job")
async def edit_job(job_id: str, body: dict = Body(...), _key: str = Depends(verify_device_key)):
    cfg, error = store.update_job(job_id, body)
    if error:
        status = 404 if "not found" in error else 422
        raise HTTPException(status, error)
    return {"ok": True, "versions": cfg["versions"], "jobs": cfg["jobs"]}


@router.delete("/ui/jobs/{job_id}", summary="Delete a cron job")
async def remove_job(job_id: str, _key: str = Depends(verify_device_key)):
    cfg = store.delete_job(job_id)
    return {"ok": True, "versions": cfg["versions"], "jobs": cfg["jobs"]}
