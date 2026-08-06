"""
ESPET Backend Server — Main Application Entry Point

FastAPI server that acts as an MCP router for the ESP32 agent.
Connects to Google's official remote MCP servers (Gmail, Calendar, Drive)
and exposes a single generic endpoint for tool execution.
"""

import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from config import settings
from api.health import router as health_router
from api.mcp_router import router as mcp_router
from api.tools_router import router as tools_router
from api.auth_router import router as auth_router
from api.ui_router import router as ui_router
from mcp.client import mcp_client_manager

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

    yield  # App is running

    # Shutdown
    logger.info("Shutting down ESPET Backend Server...")
    await mcp_client_manager.shutdown()
    logger.info("Goodbye.")


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
