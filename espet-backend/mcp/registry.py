"""
MCP Tool Registry

Auto-built lookup table that maps tool names → MCP server names.
Created at startup during the MCP discovery phase.

The registry answers: "Which MCP server owns this tool?"
"""

import logging
from dataclasses import dataclass, field

logger = logging.getLogger("espet-backend.mcp.registry")


@dataclass
class ToolSchema:
    """Schema for a single MCP tool."""

    name: str
    description: str
    input_schema: dict  # JSON Schema for the tool's parameters
    server_name: str  # Which MCP server owns this tool


class ToolRegistry:
    """
    Unified tool registry that merges tools from all MCP servers.

    Built automatically at startup — no manual tool registration needed.
    When a new MCP server is added to config, its tools appear here
    after restart with zero code changes.
    """

    def __init__(self):
        self._tools: dict[str, ToolSchema] = {}

    def register_tools(self, server_name: str, tools: list[dict]):
        """
        Register all tools discovered from an MCP server.

        Args:
            server_name: Name of the MCP server (e.g., "gmail")
            tools: List of tool schemas from the MCP server's list_tools()
        """
        for tool in tools:
            tool_name = tool.get("name", "")
            if not tool_name:
                continue

            if tool_name in self._tools:
                logger.warning(
                    f"Tool '{tool_name}' already registered by "
                    f"'{self._tools[tool_name].server_name}', "
                    f"overwriting with '{server_name}'"
                )

            self._tools[tool_name] = ToolSchema(
                name=tool_name,
                description=tool.get("description", ""),
                input_schema=tool.get("inputSchema", tool.get("input_schema", {})),
                server_name=server_name,
            )

        logger.info(
            f"Registered {len(tools)} tool(s) from MCP server '{server_name}'"
        )

    def lookup(self, tool_name: str) -> ToolSchema | None:
        """Find which server owns a tool by name."""
        return self._tools.get(tool_name)

    def get_all_tools(self) -> list[dict]:
        """
        Return all tool schemas in a format suitable for the ESP32 / LLM.

        This is what GET /api/tools returns — the ESP32 uses it to populate
        the LLM's available tools list in the system prompt.
        """
        return [
            {
                "name": t.name,
                "description": t.description,
                "input_schema": t.input_schema,
                "server": t.server_name,
            }
            for t in self._tools.values()
        ]

    def clear(self):
        """Clear all registered tools (used during re-initialization)."""
        self._tools.clear()

    def __len__(self) -> int:
        return len(self._tools)
