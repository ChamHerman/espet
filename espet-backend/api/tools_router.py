"""
Tools Router

GET /api/tools — Returns all available MCP tool schemas.

The ESP32 calls this at boot (or on-demand) to populate the LLM's
available tools list in the system prompt. This is how the LLM knows
what remote tools it can call.
"""

import logging

from fastapi import APIRouter, Depends

from auth.device_auth import verify_device_key
from mcp.client import mcp_client_manager
from api.custom_tools import CUSTOM_TOOLS

router = APIRouter(prefix="/api", tags=["tools"])
logger = logging.getLogger("espet-backend.api.tools")


@router.get(
    "/tools",
    summary="List all available MCP tools",
    description=(
        "Returns the full list of discovered MCP tools with their schemas. "
        "The ESP32 uses this to tell the LLM what remote tools are available. "
        "Called at boot or periodically to refresh the tool list."
    ),
)
async def list_tools(
    _key: str = Depends(verify_device_key),
):
    """
    Return all tool schemas from all connected MCP servers.

    Response format matches what the ESP32 agent loop expects — the tool
    schemas can be directly injected into the LLM system prompt.
    """
    tools = mcp_client_manager.registry.get_all_tools()
    
    for name, tool in CUSTOM_TOOLS.items():
        tools.append({
            "name": name,
            "description": tool["description"],
            "input_schema": tool["input_schema"],
            "server": tool["server"]
        })

    logger.info(f"Returning {len(tools)} tool(s) to ESP32")

    return {
        "tools": tools,
        "count": len(tools),
        "servers": {
            name: {
                "connected": server.connected,
                "tool_count": len(server.tools),
            }
            for name, server in mcp_client_manager.servers.items()
        },
    }
