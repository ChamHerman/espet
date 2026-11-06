"""
ESPET Web Dashboard UI Router

Serves the control dashboard at GET /. The actual page lives in
static/ (index.html, style.css, app.js) so it can be edited without
touching Python.
"""

from pathlib import Path

from fastapi import APIRouter
from fastapi.responses import FileResponse

router = APIRouter(tags=["dashboard"])

_STATIC_DIR = Path(__file__).resolve().parent.parent / "static"


@router.get(
    "/",
    response_class=FileResponse,
    summary="ESPET AI Agent Web Dashboard",
    description="Control dashboard: device status, profile, personality, memory, schedules, and MCP tools.",
)
async def dashboard():
    return FileResponse(_STATIC_DIR / "index.html")


@router.get(
    "/cloud",
    response_class=FileResponse,
    summary="Hybrid Cloud Gateway & LLM Config",
    description="Dedicated configuration page for Telegram bot token, server-side LLM relay, and chat bindings.",
)
async def cloud_dashboard():
    return FileResponse(_STATIC_DIR / "cloud.html")
