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
  GET    /api/ui/skills         — list web-managed skill files
  POST   /api/ui/skills         — create a skill
  PUT    /api/ui/skills/{id}    — edit a skill
  DELETE /api/ui/skills/{id}    — delete a skill
  GET    /api/skills/sync       — skill bundle the ESP32 pulls (enabled only)

All endpoints require the shared device API key (Bearer), the same key the
ESP32 already uses for /api/mcp/execute. Single-user scope by design.
"""

import logging

from fastapi import APIRouter, Body, Depends, Header, HTTPException, Query

from auth.device_auth import verify_device_key
import config_store as store
from api.cloud_router import notify_device_sync, registry

router = APIRouter(prefix="/api", tags=["config"])
logger = logging.getLogger("espet-backend.api.config")


@router.get("/devices", summary="List known devices and latest active device")
async def list_devices(_key: str = Depends(verify_device_key)):
    return {
        "devices": registry.list_devices(),
        "latest_device_id": registry.get_latest_device_id(),
    }


@router.post("/config/sync-now", summary="Trigger immediate device config sync")
async def post_sync_now(
    body: dict = Body(default={}),
    device_id: str | None = Query(None),
    _key: str = Depends(verify_device_key),
):
    did = device_id or body.get("device_id") or registry.get_latest_device_id()
    pushed = await notify_device_sync(did)
    return {"ok": True, "device_id": did, "pushed": bool(pushed)}


@router.get(
    "/config",
    summary="Full configuration bundle",
    description=(
        "Everything the device pulls: profile/personality/memory markdown, "
        "device settings (with POSIX TZ for the ESP32), and web-managed cron "
        "jobs. Per-section versions let the device apply only what changed."
    ),
)
async def get_config(
    device_id: str | None = Query(None),
    x_device_id: str | None = Header(None, alias="X-Device-Id"),
    _key: str = Depends(verify_device_key),
):
    did = device_id or x_device_id or registry.get_latest_device_id()
    return store.device_bundle(store.get_config(did))


@router.get("/config/version", summary="Per-section config versions")
async def get_config_version(
    device_id: str | None = Query(None),
    x_device_id: str | None = Header(None, alias="X-Device-Id"),
    _key: str = Depends(verify_device_key),
):
    did = device_id or x_device_id or registry.get_latest_device_id()
    cfg = store.get_config(did)
    return {"versions": cfg["versions"], "device": cfg["device"]}


@router.put("/ui/profile", summary="Update USER.md (profile)")
async def put_profile(
    body: dict = Body(...),
    device_id: str | None = Query(None),
    _key: str = Depends(verify_device_key),
):
    did = device_id or body.get("device_id") or registry.get_latest_device_id()
    content = body.get("user_md")
    if content is None or not content.strip():
        raise HTTPException(422, "user_md is required")
    cfg = store.update_markdown("user", content, did)
    await notify_device_sync(did)
    return {"ok": True, "versions": cfg["versions"]}


@router.put("/ui/soul", summary="Update SOUL.md (personality)")
async def put_soul(
    body: dict = Body(...),
    device_id: str | None = Query(None),
    _key: str = Depends(verify_device_key),
):
    did = device_id or body.get("device_id") or registry.get_latest_device_id()
    content = body.get("soul_md")
    if content is None or not content.strip():
        raise HTTPException(422, "soul_md is required")
    cfg = store.update_markdown("soul", content, did)
    await notify_device_sync(did)
    return {"ok": True, "versions": cfg["versions"]}


@router.put("/ui/memory", summary="Update MEMORY.md (long-term memory)")
async def put_memory(
    body: dict = Body(...),
    device_id: str | None = Query(None),
    _key: str = Depends(verify_device_key),
):
    did = device_id or body.get("device_id") or registry.get_latest_device_id()
    content = body.get("memory_md")
    if content is None:
        raise HTTPException(422, "memory_md is required")
    cfg = store.update_markdown("memory", content, did)
    await notify_device_sync(did)
    return {"ok": True, "versions": cfg["versions"]}


@router.put("/ui/device", summary="Update device settings")
async def put_device(
    body: dict = Body(...),
    device_id: str | None = Query(None),
    _key: str = Depends(verify_device_key),
):
    did = device_id or body.get("device_id") or registry.get_latest_device_id()
    cfg = store.update_device_settings(
        name=body.get("name", ""),
        timezone_iana=body.get("timezone", ""),
        sync_interval_s=body.get("sync_interval_s", 0),
        device_id=did,
    )
    await notify_device_sync(did)
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
async def list_jobs(
    device_id: str | None = Query(None),
    _key: str = Depends(verify_device_key),
):
    did = device_id or registry.get_latest_device_id()
    return {"jobs": store.get_config(did)["jobs"]}


@router.post("/ui/jobs", summary="Create a cron job")
async def create_job(
    body: dict = Body(...),
    device_id: str | None = Query(None),
    _key: str = Depends(verify_device_key),
):
    did = device_id or body.get("device_id") or registry.get_latest_device_id()
    cfg, error = store.add_job(body, did)
    if error:
        raise HTTPException(422, error)
    await notify_device_sync(did)
    return {"ok": True, "versions": cfg["versions"], "jobs": cfg["jobs"]}


@router.put("/ui/jobs/{job_id}", summary="Edit a cron job")
async def edit_job(
    job_id: str,
    body: dict = Body(...),
    device_id: str | None = Query(None),
    _key: str = Depends(verify_device_key),
):
    did = device_id or body.get("device_id") or registry.get_latest_device_id()
    cfg, error = store.update_job(job_id, body, did)
    if error:
        status = 404 if "not found" in error else 422
        raise HTTPException(status, error)
    await notify_device_sync(did)
    return {"ok": True, "versions": cfg["versions"], "jobs": cfg["jobs"]}


@router.delete("/ui/jobs/{job_id}", summary="Delete a cron job")
async def remove_job(
    job_id: str,
    device_id: str | None = Query(None),
    _key: str = Depends(verify_device_key),
):
    did = device_id or registry.get_latest_device_id()
    cfg = store.delete_job(job_id, did)
    await notify_device_sync(did)
    return {"ok": True, "versions": cfg["versions"], "jobs": cfg["jobs"]}

# ── Skills (web-managed skill markdown files) ─────────────────────

@router.get("/ui/skills", summary="List web-managed skills")
async def list_skills(
    device_id: str | None = Query(None),
    _key: str = Depends(verify_device_key),
):
    did = device_id or registry.get_latest_device_id()
    return {"skills": store.get_config(did)["skills"]}


@router.post("/ui/skills", summary="Create a skill")
async def create_skill(
    body: dict = Body(...),
    device_id: str | None = Query(None),
    _key: str = Depends(verify_device_key),
):
    did = device_id or body.get("device_id") or registry.get_latest_device_id()
    cfg, error = store.add_skill(body, did)
    if error:
        raise HTTPException(422, error)
    await notify_device_sync(did)
    return {"ok": True, "versions": cfg["versions"], "skills": cfg["skills"]}


@router.put("/ui/skills/{skill_id}", summary="Edit a skill")
async def edit_skill(
    skill_id: str,
    body: dict = Body(...),
    device_id: str | None = Query(None),
    _key: str = Depends(verify_device_key),
):
    did = device_id or body.get("device_id") or registry.get_latest_device_id()
    cfg, error = store.update_skill(skill_id, body, did)
    if error:
        status = 404 if "not found" in error else 422
        raise HTTPException(status, error)
    await notify_device_sync(did)
    return {"ok": True, "versions": cfg["versions"], "skills": cfg["skills"]}


@router.delete("/ui/skills/{skill_id}", summary="Delete a skill")
async def remove_skill(
    skill_id: str,
    device_id: str | None = Query(None),
    _key: str = Depends(verify_device_key),
):
    did = device_id or registry.get_latest_device_id()
    cfg, error = store.delete_skill(skill_id, did)
    if error:
        raise HTTPException(404, error)
    await notify_device_sync(did)
    return {"ok": True, "versions": cfg["versions"], "skills": cfg["skills"]}


@router.get(
    "/skills/sync",
    summary="Skill bundle for the device",
    description=(
        "Full markdown content of every enabled skill. The ESP32 pulls this "
        "only when versions.skills in GET /api/config is newer than the last "
        "applied version, then writes each file into /spiffs/skills/."
    ),
)
async def skills_sync(
    device_id: str | None = Query(None),
    x_device_id: str | None = Header(None, alias="X-Device-Id"),
    _key: str = Depends(verify_device_key),
):
    did = device_id or x_device_id or registry.get_latest_device_id()
    return store.skills_sync_bundle(store.get_config(did))
