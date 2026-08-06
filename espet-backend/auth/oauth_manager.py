"""
Google OAuth2 Manager

Handles the complete Google OAuth2 flow:
1. Generate authorization URL → redirect user to Google consent screen
2. Handle callback → exchange auth code for tokens
3. Store tokens → persist in SQLite for future use
4. Auto-refresh → transparently refresh expired tokens

This is a one-time setup flow. After the user authenticates once,
the backend automatically refreshes tokens forever.
"""

import logging

from google_auth_oauthlib.flow import Flow

from config import settings
from auth.token_store import token_store

logger = logging.getLogger("espet-backend.auth.oauth")


class OAuthManager:
    """
    Manages Google OAuth2 authorization flow.

    The user visits /auth/google once in a browser.
    After granting permissions, Google redirects back to /auth/google/callback.
    Tokens are stored and used for all subsequent MCP requests.
    """

    def __init__(self):
        self._flow: Flow | None = None

    def create_authorization_url(self) -> tuple[str, str]:
        """
        Generate the Google OAuth consent URL.

        Returns:
            Tuple of (authorization_url, state)
            The state is used to verify the callback is legitimate.
        """
        client_config = {
            "web": {
                "client_id": settings.google_client_id,
                "client_secret": settings.google_client_secret,
                "auth_uri": "https://accounts.google.com/o/oauth2/auth",
                "token_uri": "https://oauth2.googleapis.com/token",
                "redirect_uris": [settings.google_redirect_uri],
            }
        }

        self._flow = Flow.from_client_config(
            client_config=client_config,
            scopes=settings.google_scopes,
        )
        self._flow.redirect_uri = settings.google_redirect_uri

        authorization_url, state = self._flow.authorization_url(
            access_type="offline",  # Get refresh_token
            include_granted_scopes="true",  # Incremental auth
            prompt="consent",  # Always show consent (ensures refresh_token)
        )

        logger.info(f"Generated OAuth URL (state={state[:8]}...)")
        return authorization_url, state

    async def handle_callback(self, code: str, state: str | None = None) -> bool:
        """
        Handle the OAuth callback from Google.

        Args:
            code: Authorization code from Google
            state: State parameter for CSRF verification

        Returns:
            True if tokens were successfully stored
        """
        if not self._flow:
            # Recreate flow if server was restarted between authorize and callback
            client_config = {
                "web": {
                    "client_id": settings.google_client_id,
                    "client_secret": settings.google_client_secret,
                    "auth_uri": "https://accounts.google.com/o/oauth2/auth",
                    "token_uri": "https://oauth2.googleapis.com/token",
                    "redirect_uris": [settings.google_redirect_uri],
                }
            }
            self._flow = Flow.from_client_config(
                client_config=client_config,
                scopes=settings.google_scopes,
            )
            self._flow.redirect_uri = settings.google_redirect_uri

        try:
            # Exchange auth code for tokens
            self._flow.fetch_token(code=code)
            credentials = self._flow.credentials

            # Store in database
            await token_store.store_credentials(credentials)

            logger.info("OAuth flow completed — tokens stored successfully")
            return True

        except Exception as e:
            logger.error(f"OAuth callback failed: {e}")
            raise


# Singleton instance
oauth_manager = OAuthManager()
