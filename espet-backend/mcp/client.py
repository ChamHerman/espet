"""
MCP Client Manager

Manages connections to multiple MCP servers (Google's official remote endpoints).
Handles auto-discovery of tools, routing of tool calls, and connection lifecycle.

Architecture:
  - On startup, connects to each MCP server listed in config/mcp_servers.json
  - Each server connection discovers available tools via list_tools()
  - All tools are merged into a unified ToolRegistry
  - call_tool() automatically routes to the correct server

For Google's official remote MCP servers, we use Streamable HTTP transport
with OAuth2 Bearer token authentication.
"""

import json
import logging
from dataclasses import dataclass, field
from pathlib import Path

import httpx

from mcp.registry import ToolRegistry
from auth.token_store import token_store

logger = logging.getLogger("espet-backend.mcp.client")

# Config file path
CONFIG_PATH = Path(__file__).parent.parent / "config" / "mcp_servers.json"


@dataclass
class MCPServerConnection:
    """Represents a connection to a single MCP server."""

    name: str
    url: str  # e.g., "https://gmailmcp.googleapis.com/mcp/v1"
    connected: bool = False
    tools: list[dict] = field(default_factory=list)
    _http_client: httpx.AsyncClient | None = field(default=None, repr=False)

    async def connect(self, access_token: str | None = None):
        """
        Connect to the remote MCP server and discover its tools.

        For Google's official MCP servers, this sends a tools/list request
        to the server's HTTP endpoint with OAuth Bearer authentication.
        """
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }
        if access_token:
            headers["Authorization"] = f"Bearer {access_token}"

        self._http_client = httpx.AsyncClient(
            headers=headers,
            timeout=30.0,
        )

        try:
            # MCP Streamable HTTP: POST with JSON-RPC to list tools
            response = await self._http_client.post(
                self.url,
                json={
                    "jsonrpc": "2.0",
                    "id": 1,
                    "method": "tools/list",
                    "params": {},
                },
            )

            if response.status_code == 401:
                logger.warning(
                    f"MCP server '{self.name}' returned 401 — "
                    f"OAuth token may be expired or missing. "
                    f"Visit /auth/google to re-authenticate."
                )
                self.connected = False
                return

            # NOTE: Google's MCP servers return HTTP 403 even for successful
            # responses — the actual result is always in the JSON body.
            # Only raise on truly fatal errors (5xx, network errors).
            if response.status_code >= 500:
                response.raise_for_status()

            data = response.json()

            # Check if we got a valid JSON-RPC result
            if "result" not in data and "error" in data:
                logger.error(
                    f"MCP server '{self.name}' returned JSON-RPC error: "
                    f"{data['error']}"
                )
                self.connected = False
                return

            # Extract tools from JSON-RPC result
            result = data.get("result", {})
            self.tools = result.get("tools", [])
            self.connected = True

            logger.info(
                f"Connected to MCP server '{self.name}' — "
                f"discovered {len(self.tools)} tool(s)"
            )

        except httpx.HTTPStatusError as e:
            logger.error(
                f"Failed to connect to MCP server '{self.name}': "
                f"HTTP {e.response.status_code}"
            )
            self.connected = False

        except Exception as e:
            logger.error(
                f"Failed to connect to MCP server '{self.name}': {e}"
            )
            self.connected = False

    async def call_tool(self, tool_name: str, arguments: dict, access_token: str | None = None) -> dict:
        """
        Call a tool on this MCP server via JSON-RPC over HTTP.

        Args:
            tool_name: The tool to call (e.g., "gmail_send_email")
            arguments: Tool-specific arguments
            access_token: OAuth access token for authentication

        Returns:
            The tool's result as a dict
        """
        if not self._http_client:
            raise RuntimeError(f"MCP server '{self.name}' is not connected")

        # Update auth header if token provided
        headers = {}
        if access_token:
            headers["Authorization"] = f"Bearer {access_token}"

        response = await self._http_client.post(
            self.url,
            json={
                "jsonrpc": "2.0",
                "id": 2,
                "method": "tools/call",
                "params": {
                    "name": tool_name,
                    "arguments": arguments,
                },
            },
            headers=headers,
        )
        # Google MCP returns HTTP 403 even on success — check JSON body
        if response.status_code >= 500:
            response.raise_for_status()
        data = response.json()

        # Check for JSON-RPC error
        if "error" in data:
            error = data["error"]
            raise RuntimeError(
                f"MCP tool '{tool_name}' error: "
                f"[{error.get('code', '?')}] {error.get('message', 'Unknown error')}"
            )

        result = data.get("result", {})

        # MCP tool results contain 'content' array
        # Extract text content for simpler ESP32 consumption
        content = result.get("content", [])
        if content and isinstance(content, list):
            # Flatten text content
            text_parts = []
            for item in content:
                if isinstance(item, dict) and item.get("type") == "text":
                    text_parts.append(item.get("text", ""))

            if text_parts:
                return {"result": "\n".join(text_parts)}

        return {"result": result}

    async def disconnect(self):
        """Close the HTTP client connection."""
        if self._http_client:
            await self._http_client.aclose()
            self._http_client = None
        self.connected = False
        logger.info(f"Disconnected from MCP server '{self.name}'")


class MCPClientManager:
    """
    Manages all MCP server connections and provides tool routing.

    This is the heart of the backend — it answers:
      "ESP32 wants to call tool X. Which MCP server handles it?"
    """

    def __init__(self):
        self.servers: dict[str, MCPServerConnection] = {}
        self.registry = ToolRegistry()
        self._initialized = False

    async def initialize(self):
        """
        Connect to all MCP servers listed in config and build the tool registry.

        Reads config/mcp_servers.json, creates connections, discovers tools.
        """
        self.registry.clear()
        self.servers.clear()

        # Load config
        config = self._load_config()
        if not config.get("servers"):
            logger.warning("No MCP servers configured in mcp_servers.json")
            self._initialized = True
            return

        # Get Google OAuth access token (if available)
        access_token = await self._get_google_token()

        # Connect to each server
        for server_config in config["servers"]:
            name = server_config.get("name", "unknown")
            url = server_config.get("url", "")

            if not url:
                logger.warning(f"MCP server '{name}' has no URL, skipping")
                continue

            server = MCPServerConnection(name=name, url=url)
            await server.connect(access_token=access_token)

            self.servers[name] = server

            # Register discovered tools
            if server.tools:
                self.registry.register_tools(name, server.tools)

        self._initialized = True

    async def call_tool(self, tool_name: str, arguments: dict) -> dict:
        """
        Route a tool call to the correct MCP server.

        This is what POST /api/mcp/execute calls. No tool-specific logic here —
        pure routing via the auto-built registry.

        Args:
            tool_name: Name of the tool (e.g., "gmail_send_email")
            arguments: Tool arguments from the ESP32

        Returns:
            Tool result as a dict

        Raises:
            ValueError: If the tool is not found in any MCP server
            RuntimeError: If the owning server is not connected
        """
        # Look up which server owns this tool
        tool_entry = self.registry.lookup(tool_name)
        if not tool_entry:
            available = [t["name"] for t in self.registry.get_all_tools()]
            raise ValueError(
                f"Tool '{tool_name}' not found in registry. "
                f"Available tools: {available}"
            )

        server_name = tool_entry.server_name
        server = self.servers.get(server_name)
        if not server or not server.connected:
            raise RuntimeError(
                f"MCP server '{server_name}' (owner of tool '{tool_name}') "
                f"is not connected"
            )

        # Get fresh OAuth token
        access_token = await self._get_google_token()

        logger.info(f"Routing tool '{tool_name}' → MCP server '{server_name}'")
        result = await server.call_tool(tool_name, arguments, access_token)
        logger.info(f"Tool '{tool_name}' completed successfully")
        return result

    async def shutdown(self):
        """Gracefully disconnect all MCP servers."""
        for server in self.servers.values():
            await server.disconnect()
        self.registry.clear()
        self._initialized = False

    def _load_config(self) -> dict:
        """Load MCP server configuration from JSON file."""
        if not CONFIG_PATH.exists():
            logger.warning(f"MCP config not found at {CONFIG_PATH}")
            return {"servers": []}

        try:
            with open(CONFIG_PATH) as f:
                config = json.load(f)
            logger.info(
                f"Loaded {len(config.get('servers', []))} MCP server(s) from config"
            )
            return config
        except Exception as e:
            logger.error(f"Failed to load MCP config: {e}")
            return {"servers": []}

    async def _get_google_token(self) -> str | None:
        """
        Get a valid Google OAuth access token.
        Returns None if no token is available (user hasn't authenticated yet).
        """
        try:
            credentials = await token_store.get_valid_credentials()
            if credentials and credentials.token:
                return credentials.token
            else:
                logger.warning(
                    "No valid Google OAuth token — "
                    "visit /auth/google to authenticate"
                )
                return None
        except Exception as e:
            logger.warning(f"Failed to get Google token: {e}")
            return None


# Singleton instance — imported by routers and main.py
mcp_client_manager = MCPClientManager()
