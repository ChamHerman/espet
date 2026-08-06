"""
Auth Router — Google OAuth Endpoints

GET  /auth/google          → Redirects to Google consent screen
GET  /auth/google/callback → Handles OAuth callback, stores tokens
GET  /auth/status          → Check if authenticated

One-time setup: user opens /auth/google in browser, grants permissions,
tokens are stored, and all future MCP requests use them automatically.
"""

import logging
import httpx

from fastapi import APIRouter, HTTPException, status
from fastapi.responses import RedirectResponse, HTMLResponse

from auth.oauth_manager import oauth_manager
from auth.token_store import token_store

router = APIRouter(prefix="/auth", tags=["auth"])
logger = logging.getLogger("espet-backend.api.auth")


@router.get(
    "/google",
    summary="Start Google OAuth flow",
    description="Redirects the user to Google's consent screen to authorize access.",
)
async def google_login():
    """
    Initiate Google OAuth2 authorization.

    The user visits this URL in a browser. They'll be redirected to Google
    to grant permissions for Gmail, Calendar, and Drive. After granting,
    Google redirects back to /auth/google/callback.

    This only needs to happen ONCE — tokens are stored and refreshed
    automatically after that.
    """
    try:
        authorization_url, state = oauth_manager.create_authorization_url()
        logger.info("Redirecting to Google OAuth consent screen")
        return RedirectResponse(url=authorization_url)

    except Exception as e:
        logger.error(f"Failed to create OAuth URL: {e}")
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Failed to initiate OAuth flow: {e}",
        )


@router.get(
    "/google/callback",
    summary="Google OAuth callback",
    description="Handles the redirect from Google after user grants permissions.",
)
async def google_callback(code: str, state: str | None = None):
    """
    Handle the OAuth callback from Google.

    Google redirects here with an authorization code.
    We exchange the code for access_token + refresh_token,
    store them in the database, and show a success page.
    """
    try:
        success = await oauth_manager.handle_callback(code, state)

        if success:
            logger.info("Google OAuth completed successfully")
            return HTMLResponse(content="""
                <!DOCTYPE html>
                <html>
                <head>
                    <title>ESPET — Authentication Complete</title>
                    <style>
                        body {
                            font-family: -apple-system, BlinkMacSystemFont,
                                'Segoe UI', Roboto, sans-serif;
                            display: flex;
                            justify-content: center;
                            align-items: center;
                            height: 100vh;
                            margin: 0;
                            background: #0f172a;
                            color: #e2e8f0;
                        }
                        .card {
                            background: #1e293b;
                            padding: 3rem;
                            border-radius: 1rem;
                            text-align: center;
                            max-width: 400px;
                            box-shadow: 0 25px 50px rgba(0,0,0,0.25);
                        }
                        .icon { font-size: 4rem; margin-bottom: 1rem; }
                        h1 { color: #34d399; margin-bottom: 0.5rem; }
                        p { color: #94a3b8; line-height: 1.6; }
                        .close-note {
                            margin-top: 1.5rem;
                            font-size: 0.85rem;
                            color: #64748b;
                        }
                    </style>
                </head>
                <body>
                    <div class="card">
                        <div class="icon">✅</div>
                        <h1>Authentication Complete!</h1>
                        <p>
                            Google OAuth tokens have been stored securely.
                            Your ESPET device can now access Gmail, Calendar,
                            and Drive through the backend server.
                        </p>
                        <p class="close-note">
                            You can close this tab.
                        </p>
                    </div>
                </body>
                </html>
            """)
        else:
            raise HTTPException(
                status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
                detail="OAuth flow completed but token storage failed",
            )

    except HTTPException:
        raise

    except Exception as e:
        logger.error(f"OAuth callback failed: {e}")
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"OAuth callback failed: {e}",
        )


@router.get(
    "/status",
    summary="Check authentication status",
    description="Returns whether Google OAuth tokens are available.",
)
async def auth_status():
    """
    Check if the backend has valid Google OAuth credentials and return signed-in email.
    """
    has_creds = await token_store.has_credentials()

    if has_creds:
        credentials = await token_store.get_credentials()
        email = None
        try:
            if credentials and credentials.token:
                async with httpx.AsyncClient() as client:
                    res = await client.get(
                        "https://gmail.googleapis.com/gmail/v1/users/me/profile",
                        headers={"Authorization": f"Bearer {credentials.token}"},
                        timeout=5.0,
                    )
                    if res.status_code == 200:
                        email = res.json().get("emailAddress")
                    else:
                        res2 = await client.get(
                            "https://www.googleapis.com/oauth2/v2/userinfo",
                            headers={"Authorization": f"Bearer {credentials.token}"},
                            timeout=5.0,
                        )
                        if res2.status_code == 200:
                            email = res2.json().get("email")
        except Exception as e:
            logger.warning(f"Could not fetch Google email profile: {e}")

        return {
            "authenticated": True,
            "email": email or "Google Account Signed In",
            "has_refresh_token": bool(
                credentials.refresh_token if credentials else False
            ),
            "token_valid": bool(credentials.valid if credentials else False),
            "message": f"Google OAuth credentials are stored and ready ({email or 'Connected'}).",
        }
    else:
        return {
            "authenticated": False,
            "email": None,
            "has_refresh_token": False,
            "token_valid": False,
            "message": (
                "No Google OAuth credentials found. "
                "Visit /auth/google to authenticate."
            ),
        }
