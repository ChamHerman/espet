# Standard Operating Procedure (SOP): Adding New Tools to ESPET Backend

When you want to give the ESPET AI agent new capabilities (like Notion, Github, or new Google tools), follow this procedure. 

There are two types of tools: **Remote MCP Servers** (the default) and **Custom Backend Tools** (for custom wrappers).

---

## Type 1: Adding a Remote MCP Server
*Use this for 99% of new tools (e.g. Google Docs, Github, Notion).*

### 1. Enable the Required API
If the MCP server requires a third-party API (like Google Docs), ensure the API is enabled in the Google Cloud Console or the respective developer portal.

### 2. Update Configuration (`config/mcp_servers.json`)
Append the new MCP server details to the `servers` array in `espet-backend/config/mcp_servers.json`.

```json
{
  "name": "google-docs",
  "url": "https://docsmcp.googleapis.com/mcp/v1",
  "description": "Google Docs — read, create, write documents",
  "scopes": [
    "https://www.googleapis.com/auth/documents"
  ]
}
```

### 3. Update the UI Dashboard (`api/ui_router.py`)
To ensure the new tools show up correctly in the Web UI:
1. **Filter Button**: Add a `<button class="filter-btn" data-filter="docs">Docs</button>` inside the `.filter-pills` div.
2. **Category Mapper**: In the `<script>` tag, update the `getCategory(toolName)` JavaScript function to map the new tool names to your category filter name.
3. **Icon**: In the `renderTools()` / MCP list iteration, add an emoji icon mapping for the new server name.

### 4. Restart Backend
Restart the backend server. The `mcp_client_manager` will automatically discover the new tools on boot and populate the global registry. No C-code updates on the ESP32 are needed!

---

## Type 2: Adding a Custom Backend Wrapper Tool
*Use this ONLY if you need custom Python logic to parse arguments, wrap non-MCP APIs, or handle complex workflows that the LLM struggles with directly.*

### 1. Create the Tool Logic (`api/custom_tools.py`)
Add your tool implementation to `api/custom_tools.py`.
```python
async def my_custom_tool_executor(arguments: dict):
    # Your custom Python logic here
    return {"status": "success"}

# Register it
CUSTOM_TOOLS["my_custom_tool"] = {
    "executor": my_custom_tool_executor
}
```

### 2. Update UI manually (Optional)
Custom tools do not automatically appear via the MCP `tools/list` endpoint. If you want them visible in the UI, you must manually mock their schemas in the `tools_router.py` endpoint or keep them invisible as background wrappers.

---
*Note: As of Aug 2026, the backend uses a Hybrid Normalizer/Auto-Recovery pipeline, which dramatically reduces the need for Type 2 custom wrappers.*
