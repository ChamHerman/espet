"""
MCP Tool Router — THE Single Endpoint

POST /api/mcp/execute

This is the MOST IMPORTANT endpoint in the entire backend.
The ESP32 agent loop sends ALL remote tool calls here.
The backend automatically routes to the correct MCP server.

No tool-specific logic lives here — it's pure routing.
"""

import logging
import time

from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel, Field

from auth.device_auth import verify_device_key
from mcp.client import mcp_client_manager
from api.custom_tools import CUSTOM_TOOLS

router = APIRouter(prefix="/api", tags=["mcp"])
logger = logging.getLogger("espet-backend.api.mcp")

# Simple rate limiting state
_request_times: list[float] = []
_RATE_LIMIT = 30  # max requests per minute
_RATE_WINDOW = 60.0  # seconds


# ---------------------------------------------------------------------------
# Request / Response Models
# ---------------------------------------------------------------------------

class MCPExecuteRequest(BaseModel):
    """
    Request body for tool execution.

    The ESP32 sends exactly this:
    {
        "tool_name": "gmail_send_email",
        "arguments": {
            "to": "someone@example.com",
            "subject": "Hello",
            "body": "World"
        }
    }
    """

    tool_name: str = Field(
        ...,
        description="Name of the MCP tool to call",
        examples=["gmail_send_email", "calendar_create_event"],
    )
    arguments: dict = Field(
        default_factory=dict,
        description="Tool-specific arguments",
    )


class MCPExecuteResponse(BaseModel):
    """Response from tool execution."""

    success: bool
    result: dict | None = None
    error: str | None = None


# ---------------------------------------------------------------------------
# Rate Limiter
# ---------------------------------------------------------------------------

def _check_rate_limit():
    """Simple sliding-window rate limiter to prevent runaway agent loops."""
    now = time.time()
    # Remove entries outside the window
    while _request_times and _request_times[0] < now - _RATE_WINDOW:
        _request_times.pop(0)

    if len(_request_times) >= _RATE_LIMIT:
        raise HTTPException(
            status_code=status.HTTP_429_TOO_MANY_REQUESTS,
            detail=f"Rate limit exceeded: max {_RATE_LIMIT} requests per minute",
        )
    _request_times.append(now)


# ---------------------------------------------------------------------------
# THE Endpoint
# ---------------------------------------------------------------------------

@router.post(
    "/mcp/execute",
    response_model=MCPExecuteResponse,
    summary="Execute any MCP tool",
    description=(
        "The single generic endpoint for ALL remote tool calls from the ESP32. "
        "Receives a tool name and arguments, routes to the correct MCP server, "
        "and returns the result. No tool-specific logic — pure routing."
    ),
)
async def execute_tool(
    request: MCPExecuteRequest,
    _key: str = Depends(verify_device_key),
) -> MCPExecuteResponse:
    """
    Execute a tool via the MCP routing engine.

    Flow:
      1. ESP32 sends { tool_name, arguments }
      2. Registry looks up which MCP server owns the tool
      3. Request forwarded to that MCP server
      4. Result returned to ESP32

    No if/else, no switch statement, no hardcoded tool names.
    """
    _check_rate_limit()

    logger.info(
        f"Tool execution request: {request.tool_name} "
        f"(args: {list(request.arguments.keys())})"
    )

    try:
        if request.tool_name in CUSTOM_TOOLS:
            logger.info(f"Executing custom backend tool: {request.tool_name}")
            result = await CUSTOM_TOOLS[request.tool_name]["executor"](request.arguments)
            return MCPExecuteResponse(success=True, result=result)

        result = await mcp_client_manager.call_tool(
            request.tool_name, request.arguments
        )
        return MCPExecuteResponse(success=True, result=result)

    except ValueError as e:
        # Tool not found in registry
        logger.warning(f"Tool not found: {e}")
        return MCPExecuteResponse(success=False, error=str(e))

    except RuntimeError as e:
        # Server not connected or MCP error
        logger.error(f"Tool execution failed: {e}")
        return MCPExecuteResponse(success=False, error=str(e))

    except Exception as e:
        # Unexpected error
        logger.exception(f"Unexpected error executing tool '{request.tool_name}'")
        return MCPExecuteResponse(
            success=False,
            error=f"Internal error: {type(e).__name__}: {e}",
        )
