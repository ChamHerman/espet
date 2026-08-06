"""
Health Check Endpoint

Simple health check that reports server status and connected MCP services.
Used by ESP32 and monitoring tools.
"""

import logging
from datetime import datetime, timezone

from fastapi import APIRouter

router = APIRouter(tags=["health"])
logger = logging.getLogger("espet-backend.health")


@router.get("/health")
async def health_check():
    """
    Server health check.

    Returns server status, uptime info, and connected MCP server state.
    ESP32 can call this periodically to verify the backend is reachable.
    """
    from mcp.client import mcp_client_manager

    # Build MCP server status report
    mcp_status = {}
    for name, server in mcp_client_manager.servers.items():
        mcp_status[name] = {
            "connected": server.connected,
            "tool_count": len(server.tools),
        }

    return {
        "status": "healthy",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "version": "0.1.0",
        "mcp_servers": mcp_status,
        "total_tools": len(mcp_client_manager.registry.get_all_tools()),
    }
