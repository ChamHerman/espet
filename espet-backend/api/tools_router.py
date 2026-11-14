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

# Low-value, overly complex, or administrative tools excluded from LLM prompt
EXCLUDED_MCP_TOOLS = {
    # Gmail spam & sensitive labels
    "mark_thread_spam",
    "unmark_thread_spam",
    "mark_message_spam",
    "unmark_message_spam",
    "apply_sensitive_thread_label",
    "apply_sensitive_message_label",
    # Gmail label administrative management
    "create_label",
    "list_labels",
    "label_message",
    "unlabel_message",
    "update_message_labels",
    "label_thread",
    "unlabel_thread",
    # Gmail trash & fine-grained drafts (clean sending is handled by gmail_send_email)
    "trash_thread",
    "untrash_thread",
    "trash_message",
    "untrash_message",
    "create_draft",
    "list_drafts",
    "get_draft",
    # Google Drive & Docs overly complex / binary / ACL tools
    "get_file_permissions",
    "download_file_content",
    "update_doc",
    "copy_file",
}


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
    # 1. Custom tools take precedence over raw MCP tools with identical names
    custom_names = set(CUSTOM_TOOLS.keys())
    raw_mcp_tools = mcp_client_manager.registry.get_all_tools()
    filtered_mcp = [
        t for t in raw_mcp_tools
        if t.get("name") not in custom_names and t.get("name") not in EXCLUDED_MCP_TOOLS
    ]

    tools = []

    # Add custom backend tools first (these have streamlined schemas tailored for ESPET)
    for name, tool in CUSTOM_TOOLS.items():
        schema = tool.get("input_schema") or {}
        tools.append({
            "name": name,
            "description": tool["description"],
            "input_schema": schema,
            "server": tool["server"]
        })

    # Add discovered MCP tools with cleaned/truncated descriptions
    for t in filtered_mcp:
        desc = (t.get("description") or "").strip()
        # Truncate overly verbose descriptions (e.g. update_doc has 36KB documentation)
        if len(desc) > 300:
            cut = desc[:300].rsplit(". ", 1)[0]
            desc = (cut + ".") if len(cut) > 50 else desc[:297] + "..."

        schema = t.get("input_schema") or {}
        tools.append({
            "name": t.get("name"),
            "description": desc,
            "input_schema": schema,
            "server": t.get("server") or "mcp"
        })

    logger.info(f"Returning {len(tools)} tool(s) to ESP32 (custom={len(CUSTOM_TOOLS)}, mcp={len(filtered_mcp)})")

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
