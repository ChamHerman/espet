"""
ESPET Backend Server — Main Application Entry Point

FastAPI server that acts as an MCP router for the ESP32 agent.
Connects to Google's official remote MCP servers (Gmail, Calendar, Drive)
and exposes a single generic endpoint for tool execution.
"""

import logging
import socket
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from config import settings
from api.health import router as health_router
from api.mcp_router import router as mcp_router
from api.tools_router import router as tools_router
from api.auth_router import router as auth_router
from api.config_router import router as config_router
from api.voice_router import router as voice_router
from api.ui_router import router as ui_router
from api.cloud_router import router as cloud_router
from mcp.client import mcp_client_manager
from config import settings

STATIC_DIR = Path(__file__).resolve().parent / "static"

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("espet-backend")


# ---------------------------------------------------------------------------
# Application Lifespan (startup / shutdown)
# ---------------------------------------------------------------------------
@asynccontextmanager
async def lifespan(app: FastAPI):
    """
    On startup:
      - Connect to all configured Google MCP servers
      - Build unified tool registry via auto-discovery
    On shutdown:
      - Gracefully disconnect MCP servers
    """
    logger.info("=" * 60)
    logger.info("ESPET Backend Server starting up...")
    logger.info("=" * 60)

    # Initialize MCP client connections
    try:
        await mcp_client_manager.initialize()
        tool_count = len(mcp_client_manager.registry.get_all_tools())
        logger.info(f"MCP ready — {tool_count} tools discovered from "
                    f"{len(mcp_client_manager.servers)} server(s)")
    except Exception as e:
        logger.warning(f"MCP initialization failed (will retry on first request): {e}")

    logger.info(f"Server listening on {settings.host}:{settings.port}")
    logger.info("=" * 60)

    # Advertise the backend over mDNS/Bonjour as "_espet._tcp.local." so the
    # ESP32 can discover this machine's current IP at runtime (backend_discovery.c)
    mdns_zc = None
    mdns_info = None
    try:
        from zeroconf import ServiceInfo, Zeroconf

        lan_ip = settings.host if settings.host not in ("0.0.0.0", "") else _detect_lan_ip()
        mdns_zc = Zeroconf()
        mdns_info = ServiceInfo(
            "_espet._tcp.local.",
            "espet-backend._espet._tcp.local.",
            server="espet-backend.local.",
            addresses=[socket.inet_aton(lan_ip)],
            port=settings.port,
            properties={"version": "1"},
        )
        # async API is mandatory here: the sync one raises EventLoopBlocked
        # when called from inside uvicorn's running event loop
        await mdns_zc.async_register_service(mdns_info, allow_name_change=True)
        logger.info(f"mDNS service registered: {mdns_info.name} ({lan_ip}:{settings.port})")
    except Exception as e:
        logger.warning(f"mDNS advertisement skipped ({type(e).__name__}: {e!r}) — "
                       f"ESP32 will use compiled-in fallback IP")

    # Hybrid mode cloud gateway: poll Telegram for the device and route
    # frames over the device's persistent WebSocket (api/cloud_router.py).
    # Token/enabled come from the dashboard-editable gateway settings store
    # (falls back to .env); restart_gateway() no-ops when disabled.
    from api.cloud_router import restart_gateway
    restart_gateway()

    yield  # App is running

    # Shutdown
    logger.info("Shutting down ESPET Backend Server...")
    from services.telegram_gateway import get_gateway as _get_gw, set_gateway as _set_gw
    _gw = _get_gw()
    if _gw:
        await _gw.stop()
        _set_gw(None)
    if mdns_zc and mdns_info:
        try:
            await mdns_zc.async_unregister_service(mdns_info)
            mdns_zc.close()
        except Exception:
            pass
    await mcp_client_manager.shutdown()
    logger.info("Goodbye.")


def _detect_lan_ip() -> str:
    """Best-effort LAN IP without sending traffic: UDP 'connect' trick."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


# ---------------------------------------------------------------------------
# FastAPI App
# ---------------------------------------------------------------------------
app = FastAPI(
    title="ESPET Backend Server",
    description=(
        "MCP tool router for the ESPET ESP32-S3 embodied AI agent. "
        "Routes tool calls from the ESP32 agent loop to Google's official "
        "remote MCP servers (Gmail, Calendar, Drive)."
    ),
    version="0.1.0",
    lifespan=lifespan,
)

# CORS — allow ESP32 and local dev tools
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # Restrict in production
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Mount routers
app.include_router(ui_router)
app.include_router(health_router)
app.include_router(mcp_router)
app.include_router(tools_router)
app.include_router(auth_router)
app.include_router(config_router)
app.include_router(voice_router)
app.include_router(cloud_router)

# Alias /ws/device -> /api/cloud/ws/device for firmware compatibility
from fastapi import Query, WebSocket
from api.cloud_router import ws_device as _cloud_ws_device

@app.websocket("/ws/device")
async def ws_device_alias(websocket: WebSocket, key: str = Query("")):
    await _cloud_ws_device(websocket, key)

# Dashboard assets (css/js) — GET / itself is served by ui_router
app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")


# ---------------------------------------------------------------------------
# Run with: python main.py  or  uvicorn main:app --reload
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    import uvicorn

    uvicorn.run(
        "main:app",
        host=settings.host,
        port=settings.port,
        reload=True,
        log_level="info",
    )
