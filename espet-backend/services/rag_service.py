"""
ESPET RAG & Vector Memory Service
Powered by fastembed (BAAI/bge-small-en-v1.5) and sqlite-vec (cosine distance).
"""

from __future__ import annotations

import json
import logging
import time
from typing import Any

import sqlite_vec
from fastembed import TextEmbedding

from services.memory_db import MemoryDB

logger = logging.getLogger("espet-backend.rag")

# Model singleton so weights load once across the server process
_embedding_model: TextEmbedding | None = None


def get_embedding_model() -> TextEmbedding:
    global _embedding_model
    if _embedding_model is None:
        logger.info("Initializing fastembed TextEmbedding(BAAI/bge-small-en-v1.5)...")
        _embedding_model = TextEmbedding(model_name="BAAI/bge-small-en-v1.5")
    return _embedding_model


class RAGService:
    def __init__(self, db: MemoryDB | None = None):
        self.db = db or MemoryDB()
        self.model = get_embedding_model()

    def get_embedding(self, text: str) -> list[float]:
        clean_text = text.strip()
        if not clean_text:
            return [0.0] * 384
        embeddings = list(self.model.embed([clean_text]))
        return embeddings[0].tolist()

    def add_memory(
        self,
        device_id: str,
        content: str,
        source_type: str = "fact",
        source_ref: str = "web_ui",
        user_id: str = "default_user",
        metadata: dict | None = None,
    ) -> int:
        content_clean = content.strip()
        if not content_clean:
            raise ValueError("Memory content cannot be empty")

        meta_json = json.dumps(metadata or {})
        # Compute embedding before acquiring lock to avoid blocking other threads during ONNX inference
        vec = self.get_embedding(content_clean)

        with self.db._lock:
            conn = self.db.get_conn()
            with conn:
                cur = conn.cursor()
                cur.execute(
                    """
                    INSERT INTO memory_chunks (user_id, device_id, source_type, source_ref, content, metadata_json)
                    VALUES (?, ?, ?, ?, ?, ?)
                    """,
                    (user_id, device_id, source_type, source_ref, content_clean, meta_json),
                )
                chunk_id = cur.lastrowid

                cur.execute(
                    """
                    INSERT INTO vec_memories (chunk_id, embedding)
                    VALUES (?, ?)
                    """,
                    (chunk_id, sqlite_vec.serialize_float32(vec)),
                )
        logger.info("Added memory chunk %d (%s) for device %s", chunk_id, source_type, device_id)
        return chunk_id

    def update_memory(
        self,
        chunk_id: int,
        content: str,
        source_type: str | None = None,
    ) -> bool:
        content_clean = content.strip()
        if not content_clean:
            raise ValueError("Memory content cannot be empty")

        # Compute embedding before acquiring lock
        vec = self.get_embedding(content_clean)
        with self.db._lock:
            conn = self.db.get_conn()
            with conn:
                cur = conn.cursor()
                if source_type:
                    cur.execute(
                        """
                        UPDATE memory_chunks
                        SET content = ?, source_type = ?, updated_at = CURRENT_TIMESTAMP
                        WHERE id = ?
                        """,
                        (content_clean, source_type, chunk_id),
                    )
                else:
                    cur.execute(
                        """
                        UPDATE memory_chunks
                        SET content = ?, updated_at = CURRENT_TIMESTAMP
                        WHERE id = ?
                        """,
                        (content_clean, chunk_id),
                    )

                if cur.rowcount == 0:
                    return False

                # Replace vector embedding
                cur.execute("DELETE FROM vec_memories WHERE chunk_id = ?", (chunk_id,))
                cur.execute(
                    "INSERT INTO vec_memories (chunk_id, embedding) VALUES (?, ?)",
                    (chunk_id, sqlite_vec.serialize_float32(vec)),
                )
        return True

    def delete_memory(self, chunk_id: int) -> bool:
        with self.db._lock:
            conn = self.db.get_conn()
            with conn:
                cur = conn.cursor()
                cur.execute("DELETE FROM vec_memories WHERE chunk_id = ?", (chunk_id,))
                cur.execute("DELETE FROM memory_chunks WHERE id = ?", (chunk_id,))
                deleted = cur.rowcount > 0
        if deleted:
            logger.info("Deleted memory chunk %d", chunk_id)
        return deleted

    def delete_noisy_actions(self, device_id: str) -> int:
        """
        Clean up noisy read-only and memory query tool executions from episodic action logs.
        """
        patterns = [
            "Action: executed tool get_current_time%",
            "Action: executed tool cron_list%",
            "Action: executed tool gpio_read%",
            "Action: executed tool read_file%",
            "Action: executed tool list_dir%",
            "Action: executed tool memory_search%",
            "Action: executed tool memory_save%",
        ]
        deleted_count = 0
        with self.db._lock:
            conn = self.db.get_conn()
            with conn:
                cur = conn.cursor()
                # Find chunk IDs matching noisy patterns
                where_pattern = " OR ".join(["content LIKE ?" for _ in patterns])
                query = f"""
                    SELECT id FROM memory_chunks
                    WHERE device_id = ? AND source_type = 'action' AND ({where_pattern})
                """
                cur.execute(query, (device_id, *patterns))
                rows = cur.fetchall()
                chunk_ids = [r[0] for r in rows]
                if chunk_ids:
                    placeholders = ",".join("?" * len(chunk_ids))
                    cur.execute(f"DELETE FROM vec_memories WHERE chunk_id IN ({placeholders})", chunk_ids)
                    cur.execute(f"DELETE FROM memory_chunks WHERE id IN ({placeholders})", chunk_ids)
                    deleted_count = cur.rowcount
        logger.info("Deleted %d noisy action records for device %s", deleted_count, device_id)
        return deleted_count

    def get_memory(self, chunk_id: int) -> dict | None:
        row = self.db.fetchone(
            """
            SELECT id, user_id, device_id, source_type, source_ref, content, metadata_json, created_at, updated_at
            FROM memory_chunks WHERE id = ?
            """,
            (chunk_id,),
        )
        if not row:
            return None
        return dict(row)

    def list_memories(
        self,
        device_id: str,
        source_type: str | None = None,
        search: str | None = None,
        limit: int = 50,
        offset: int = 0,
    ) -> list[dict]:
        params: list[Any] = [device_id]
        where_clauses = ["device_id = ?"]

        if source_type and source_type.lower() != "all":
            where_clauses.append("source_type = ?")
            params.append(source_type)

        if search and search.strip():
            where_clauses.append("content LIKE ?")
            params.append(f"%{search.strip()}%")

        params.extend([limit, offset])
        query = f"""
            SELECT id, user_id, device_id, source_type, source_ref, content, metadata_json, created_at, updated_at
            FROM memory_chunks
            WHERE {' AND '.join(where_clauses)}
            ORDER BY created_at DESC
            LIMIT ? OFFSET ?
        """
        rows = self.db.fetchall(query, tuple(params))
        return [dict(r) for r in rows]

    def count_memories(
        self,
        device_id: str,
        source_type: str | None = None,
        search: str | None = None,
    ) -> int:
        params: list[Any] = [device_id]
        where_clauses = ["device_id = ?"]
        if source_type and source_type.lower() != "all":
            where_clauses.append("source_type = ?")
            params.append(source_type)
        if search and search.strip():
            where_clauses.append("content LIKE ?")
            params.append(f"%{search.strip()}%")
        row = self.db.fetchone(
            f"SELECT COUNT(*) FROM memory_chunks WHERE {' AND '.join(where_clauses)}",
            tuple(params),
        )
        return row[0] if row else 0

    def search(
        self,
        query: str,
        device_id: str,
        top_k: int = 3,
        threshold: float = 0.28,
    ) -> dict[str, Any]:
        """
        KNN semantic similarity search via sqlite-vec cosine distance.
        Cosine distance: 0.0 = identical, 1.0 = orthogonal.
        threshold=0.28 means cosine similarity >= 0.72.
        """
        t0 = time.perf_counter()
        query_vec = self.get_embedding(query)
        # Fetch candidate set to account for device_id filtering
        k_fetch = max(top_k * 5, 50)
        with self.db._lock:
            conn = self.db.get_conn()
            cur = conn.cursor()
            cur.execute(
                """
                SELECT m.id, m.content, m.source_type, m.source_ref, m.created_at, v.distance
                FROM vec_memories v
                JOIN memory_chunks m ON m.id = v.chunk_id
                WHERE v.embedding MATCH ? AND k = ? AND m.device_id = ?
                ORDER BY v.distance ASC
                """,
                (sqlite_vec.serialize_float32(query_vec), k_fetch, device_id),
            )
            rows = cur.fetchall()

        results = []
        for r in rows:
            dist = float(r["distance"])
            if dist <= threshold:
                results.append(
                    {
                        "id": r["id"],
                        "content": r["content"],
                        "source_type": r["source_type"],
                        "source_ref": r["source_ref"],
                        "created_at": r["created_at"],
                        "distance": round(dist, 4),
                        "similarity": round(1.0 - dist, 4),
                    }
                )
            if len(results) >= top_k:
                break

        latency_ms = round((time.perf_counter() - t0) * 1000, 2)
        return {
            "query": query,
            "device_id": device_id,
            "latency_ms": latency_ms,
            "count": len(results),
            "results": results,
        }

    def archive_daily_note(
        self,
        device_id: str,
        note_date: str,
        raw_content: str,
        user_id: str = "default_user",
    ) -> int:
        """
        Archives raw daily note (FR-8), parses key items, and indexes into memory_chunks.
        """
        with self.db._lock:
            conn = self.db.get_conn()
            with conn:
                cur = conn.cursor()
                cur.execute(
                    """
                    INSERT OR REPLACE INTO daily_notes_archive (user_id, device_id, note_date, raw_content, archived_at)
                    VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)
                    """,
                    (user_id, device_id, note_date, raw_content),
                )
                # Idempotent cleanup: remove previously indexed chunks for this date
                cur.execute(
                    """
                    DELETE FROM vec_memories WHERE chunk_id IN (
                        SELECT id FROM memory_chunks WHERE device_id = ? AND source_ref = ?
                    )
                    """,
                    (device_id, f"{note_date}.md"),
                )
                cur.execute(
                    "DELETE FROM memory_chunks WHERE device_id = ? AND source_ref = ?",
                    (device_id, f"{note_date}.md"),
                )
        # Parse chunks: bullets (- / *) or lines > 15 chars
        chunks = []
        for line in raw_content.splitlines():
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            if s.startswith("- ") or s.startswith("* "):
                cleaned = s[2:].strip()
                if len(cleaned) >= 5:
                    chunks.append(cleaned)
            elif len(s) >= 20:
                chunks.append(s)

        added = 0
        for chunk in chunks:
            self.add_memory(
                device_id=device_id,
                content=chunk,
                source_type="daily_note",
                source_ref=f"{note_date}.md",
                user_id=user_id,
            )
            added += 1

        logger.info("Archived daily note %s: %d chunks indexed", note_date, added)
        return added


# Global default instance
rag_service = RAGService()
