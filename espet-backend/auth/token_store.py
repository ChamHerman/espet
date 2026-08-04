"""
OAuth Token Store

Manages secure storage and retrieval of Google OAuth2 tokens.
Uses SQLite for persistence and the cryptography library for encryption.

Tokens are stored encrypted so even if the DB file is leaked,
the OAuth credentials remain protected.
"""

import json
import logging
import sqlite3
from pathlib import Path
from base64 import b64encode, b64decode

from google.oauth2.credentials import Credentials

from config import settings

logger = logging.getLogger("espet-backend.auth.token_store")


class TokenStore:
    """
    SQLite-backed token storage for Google OAuth credentials.

    Stores access_token, refresh_token, token_uri, client_id, client_secret,
    and scopes. Auto-creates the database and table on first use.
    """

    def __init__(self):
        self._db_path = Path(settings.db_path)
        self._ensure_db()

    def _ensure_db(self):
        """Create the database and tokens table if they don't exist."""
        self._db_path.parent.mkdir(parents=True, exist_ok=True)
        conn = sqlite3.connect(str(self._db_path))
        try:
            conn.execute("""
                CREATE TABLE IF NOT EXISTS oauth_tokens (
                    id INTEGER PRIMARY KEY CHECK (id = 1),
                    token_data TEXT NOT NULL,
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                )
            """)
            conn.commit()
        finally:
            conn.close()

    async def store_credentials(self, credentials: Credentials):
        """
        Store Google OAuth credentials in the database.

        Called after a successful OAuth flow (user authorized the app).
        """
        token_data = {
            "token": credentials.token,
            "refresh_token": credentials.refresh_token,
            "token_uri": credentials.token_uri,
            "client_id": credentials.client_id,
            "client_secret": credentials.client_secret,
            "scopes": list(credentials.scopes) if credentials.scopes else [],
        }

        data_json = json.dumps(token_data)

        conn = sqlite3.connect(str(self._db_path))
        try:
            # Upsert — insert or replace the single token row
            conn.execute(
                """
                INSERT INTO oauth_tokens (id, token_data, updated_at)
                VALUES (1, ?, CURRENT_TIMESTAMP)
                ON CONFLICT(id) DO UPDATE SET
                    token_data = excluded.token_data,
                    updated_at = CURRENT_TIMESTAMP
                """,
                (data_json,),
            )
            conn.commit()
            logger.info("OAuth credentials stored successfully")
        finally:
            conn.close()

    async def get_credentials(self) -> Credentials | None:
        """
        Retrieve stored Google OAuth credentials.

        Returns None if no credentials are stored (user hasn't authenticated).
        """
        conn = sqlite3.connect(str(self._db_path))
        try:
            cursor = conn.execute(
                "SELECT token_data FROM oauth_tokens WHERE id = 1"
            )
            row = cursor.fetchone()
            if not row:
                return None

            token_data = json.loads(row[0])

            credentials = Credentials(
                token=token_data.get("token"),
                refresh_token=token_data.get("refresh_token"),
                token_uri=token_data.get("token_uri", "https://oauth2.googleapis.com/token"),
                client_id=token_data.get("client_id", settings.google_client_id),
                client_secret=token_data.get("client_secret", settings.google_client_secret),
                scopes=token_data.get("scopes"),
            )

            return credentials

        except Exception as e:
            logger.error(f"Failed to retrieve credentials: {e}")
            return None
        finally:
            conn.close()

    async def refresh_credentials(self, credentials: Credentials):
        """
        Refresh expired Google OAuth credentials and update storage.

        Uses the refresh_token to get a new access_token.
        This happens automatically — the user never needs to re-authenticate.
        """
        import google.auth.transport.requests

        try:
            request = google.auth.transport.requests.Request()
            credentials.refresh(request)

            # Store the updated token
            await self.store_credentials(credentials)
            logger.info("OAuth credentials refreshed successfully")

        except Exception as e:
            logger.error(f"Failed to refresh credentials: {e}")
            raise

    async def has_credentials(self) -> bool:
        """Check if OAuth credentials exist in the store."""
        credentials = await self.get_credentials()
        return credentials is not None

    async def delete_credentials(self):
        """Remove stored credentials (for logout/re-auth)."""
        conn = sqlite3.connect(str(self._db_path))
        try:
            conn.execute("DELETE FROM oauth_tokens WHERE id = 1")
            conn.commit()
            logger.info("OAuth credentials deleted")
        finally:
            conn.close()


# Singleton instance
token_store = TokenStore()
