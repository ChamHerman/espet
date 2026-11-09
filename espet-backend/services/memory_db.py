"""
ESPET Memory Database Layer
Manages local SQLite (db/espet.db) with sqlite-vec vector extension in WAL mode.
"""

from __future__ import annotations

import logging
import sqlite3
import threading
from pathlib import Path
from typing import Any

import sqlite_vec

logger = logging.getLogger("espet-backend.memory_db")


class MemoryDB:
    """Thread-safe SQLite connection manager with sqlite-vec extension."""

    def __init__(self, db_path: str = "db/espet.db"):
        self.db_path = Path(db_path)
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.RLock()
        self._conn: sqlite3.Connection | None = None
        self._init_db()

    def get_conn(self) -> sqlite3.Connection:
        with self._lock:
            if self._conn is None:
                conn = sqlite3.connect(str(self.db_path), check_same_thread=False)
                conn.row_factory = sqlite3.Row
                conn.enable_load_extension(True)
                sqlite_vec.load(conn)
                conn.enable_load_extension(False)
                self._conn = conn
            return self._conn

    def _init_db(self) -> None:
        with self._lock:
            conn = self.get_conn()
            cur = conn.cursor()

            # Enable WAL mode for high-concurrency read/write
            cur.execute("PRAGMA journal_mode = WAL;")
            cur.execute("PRAGMA synchronous = NORMAL;")

            # 1. Devices table (NFR-9: user/device isolation)
            cur.execute("""
                CREATE TABLE IF NOT EXISTS devices (
                    device_id TEXT PRIMARY KEY,
                    user_id TEXT NOT NULL,
                    device_name TEXT DEFAULT 'ESPET',
                    bound_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                );
            """)

            # 2. Granular memory chunks
            cur.execute("""
                CREATE TABLE IF NOT EXISTS memory_chunks (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    user_id TEXT NOT NULL,
                    device_id TEXT NOT NULL,
                    source_type TEXT NOT NULL,     -- 'fact', 'preference', 'daily_note', 'action', 'conversation'
                    source_ref TEXT,               -- 'agent_tool', '2026-11-08.md', 'web_ui', 'relay'
                    chunk_index INTEGER DEFAULT 0,
                    content TEXT NOT NULL,
                    metadata_json TEXT,
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                    FOREIGN KEY (device_id) REFERENCES devices(device_id)
                );
            """)
            cur.execute("""
                CREATE INDEX IF NOT EXISTS idx_chunks_device ON memory_chunks(device_id, source_type);
            """)

            # 3. sqlite-vec virtual table with cosine distance
            cur.execute("""
                CREATE VIRTUAL TABLE IF NOT EXISTS vec_memories USING vec0(
                    chunk_id INTEGER PRIMARY KEY,
                    embedding float[384] distance_metric=cosine
                );
            """)

            # 4. Aged daily notes archive (FR-8)
            cur.execute("""
                CREATE TABLE IF NOT EXISTS daily_notes_archive (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    user_id TEXT NOT NULL,
                    device_id TEXT NOT NULL,
                    note_date TEXT NOT NULL,
                    raw_content TEXT NOT NULL,
                    archived_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                    UNIQUE(device_id, note_date)
                );
            """)

            # Seed default device if not present
            cur.execute("""
                INSERT OR IGNORE INTO devices (device_id, user_id, device_name)
                VALUES ('espet_s3_01', 'default_user', 'ESPET')
            """)

            conn.commit()
            logger.info("MemoryDB initialized at %s with sqlite-vec", self.db_path)

    def execute(self, sql: str, params: tuple = ()) -> sqlite3.Cursor:
        with self._lock:
            conn = self.get_conn()
            cur = conn.cursor()
            cur.execute(sql, params)
            conn.commit()
            return cur

    def fetchone(self, sql: str, params: tuple = ()) -> sqlite3.Row | None:
        with self._lock:
            cur = self.get_conn().cursor()
            cur.execute(sql, params)
            return cur.fetchone()

    def fetchall(self, sql: str, params: tuple = ()) -> list[sqlite3.Row]:
        with self._lock:
            cur = self.get_conn().cursor()
            cur.execute(sql, params)
            return cur.fetchall()

    def close(self) -> None:
        with self._lock:
            if self._conn:
                try:
                    self._conn.close()
                except Exception:
                    pass
                self._conn = None
