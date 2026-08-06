import logging
import re

logger = logging.getLogger("espet-backend.api.normalizer")

# Common hallucinations by small models (e.g. Gemma-4-e2b)
COMMON_ALIASES = {
    "recipient": "to",
    "email": "to",
    "start": "startTime",
    "end": "endTime",
    "title": "summary",       # Calendar uses summary, models often use title
    "desc": "description",
    "body": "text",           # Some tools use text instead of body
    "message": "text",
    "content": "text"
}

def snake_to_camel(snake_str: str) -> str:
    """Converts 'start_time' -> 'startTime'"""
    components = snake_str.split('_')
    if not components:
        return snake_str
    return components[0] + ''.join(x.title() for x in components[1:])

def normalize_arguments(tool_name: str, arguments: dict, registry) -> dict:
    """
    First pass: proactively fix parameters before sending to MCP server.
    Converts snake_case to camelCase and maps common synonyms.
    """
    if not arguments:
        return arguments

    tool_schema = registry.lookup(tool_name)
    if not tool_schema or not tool_schema.input_schema:
        return arguments

    properties = tool_schema.input_schema.get("properties", {})
    normalized = {}

    for key, val in arguments.items():
        # 1. Exact match (already correct)
        if key in properties:
            normalized[key] = val
            continue
            
        # 2. Try snake_case -> camelCase mapping
        camel_key = snake_to_camel(key)
        if camel_key in properties:
            logger.info(f"[Normalizer] Fixed '{key}' -> '{camel_key}' for tool {tool_name}")
            normalized[camel_key] = val
            continue
            
        # 3. Try common aliases (recipient -> to)
        alias_key = COMMON_ALIASES.get(key.lower())
        if alias_key and alias_key in properties:
            logger.info(f"[Normalizer] Aliased '{key}' -> '{alias_key}' for tool {tool_name}")
            normalized[alias_key] = val
            continue
            
        # 4. No mapping found, pass it through
        normalized[key] = val
        
    return normalized

def auto_recover_arguments(arguments: dict, error_message: str, tool_name: str, registry) -> dict | None:
    """
    Second pass: parse the MCP server error and attempt to fix arguments.
    Returns fixed arguments dict if a fix was applied, else None.
    """
    # Look for common MCP error patterns like: "Missing required property: to" 
    # or "Missing required property 'to'"
    missing_prop_match = re.search(r"missing required property[: ]+['\"]?([a-zA-Z0-9_]+)['\"]?", error_message, re.IGNORECASE)
    if not missing_prop_match:
        return None
        
    missing_prop = missing_prop_match.group(1)
    
    # Are there any arguments we provided that didn't match the schema?
    # We might have passed 'receiver' instead of 'to'
    tool_schema = registry.lookup(tool_name)
    if not tool_schema or not tool_schema.input_schema:
        return None
        
    properties = tool_schema.input_schema.get("properties", {})
    
    # Identify keys in arguments that are NOT in properties
    unmapped_keys = [k for k in arguments.keys() if k not in properties]
    
    if len(unmapped_keys) == 1:
        # We only have one unmapped key, and exactly one missing required property.
        # It's highly likely they correspond to each other!
        wrong_key = unmapped_keys[0]
        logger.info(f"[Auto-Recovery] Attempting to map unknown key '{wrong_key}' to missing required property '{missing_prop}'")
        
        recovered = dict(arguments)
        val = recovered.pop(wrong_key)
        recovered[missing_prop] = val
        return recovered
        
    # If there are multiple unmapped keys, we don't blindly map.
    return None
