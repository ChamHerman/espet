"""
Device Authentication Middleware

Validates the ESP32 device API key from the Authorization header.
Applied to all tool-execution and service endpoints to ensure
only authorized ESP32 devices can use the backend.
"""

import logging

from fastapi import Depends, HTTPException, status
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials

from config import settings

logger = logging.getLogger("espet-backend.auth")

# Bearer token scheme
_bearer_scheme = HTTPBearer(
    scheme_name="DeviceAPIKey",
    description="Shared API key between ESP32 and backend server",
)


async def verify_device_key(
    credentials: HTTPAuthorizationCredentials = Depends(_bearer_scheme),
) -> str:
    """
    FastAPI dependency that validates the device API key.

    Usage:
        @router.post("/api/mcp/execute")
        async def execute_tool(..., _key: str = Depends(verify_device_key)):
            ...

    The ESP32 sends:
        Authorization: Bearer <device_api_key>

    Returns the validated key string if successful.
    Raises HTTP 401 if the key is missing or incorrect.
    """
    if credentials.credentials != settings.device_api_key:
        logger.warning("Rejected request with invalid device API key")
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid device API key",
            headers={"WWW-Authenticate": "Bearer"},
        )
    return credentials.credentials
