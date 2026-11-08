"""
Memory Router — REST endpoints for Long-Term Memory (SQLite-Vec) & Daily Note Archival (FR-8).
"""

from __future__ import annotations

import asyncio
import logging
from typing import Any

from fastapi import APIRouter, Body, Depends, HTTPException, Query
from pydantic import BaseModel, Field

from auth.device_auth import verify_device_key
from services.rag_service import rag_service

logger = logging.getLogger("espet-backend.api.memory")

router = APIRouter(prefix="/api/memory", tags=["memory"])


class MemoryCreateRequest(BaseModel):
    content: str = Field(..., min_length=1, description="Memory content to save")
    category: str = Field(default="fact", description="Category: fact, preference, event, action, note")
    device_id: str = Field(default="espet_s3_01", description="Device ID")


class MemoryUpdateRequest(BaseModel):
    content: str = Field(..., min_length=1, description="Updated memory content")
    category: str | None = Field(default=None, description="Optional updated category")


class MemoryQueryRequest(BaseModel):
    query: str = Field(..., min_length=1, description="Search query")
    top_k: int = Field(default=3, ge=1, le=20, description="Top matches to return")
    threshold: float = Field(default=0.28, ge=0.0, le=2.0, description="Cosine distance threshold")
    device_id: str = Field(default="espet_s3_01", description="Device ID")


class DailyNoteArchiveRequest(BaseModel):
    note_date: str = Field(..., pattern=r"^\d{4}-\d{2}-\d{2}$", description="Date string YYYY-MM-DD")
    content: str = Field(..., min_length=1, description="Raw markdown content of daily note")
    device_id: str = Field(default="espet_s3_01", description="Device ID")


@router.get("/list", summary="List long-term memory items")
async def list_memory_items(
    device_id: str = Query("espet_s3_01"),
    category: str | None = Query(None),
    search: str | None = Query(None),
    limit: int = Query(50, ge=1, le=200),
    offset: int = Query(0, ge=0),
    _key: str = Depends(verify_device_key),
) -> dict[str, Any]:
    items = await asyncio.to_thread(
        rag_service.list_memories,
        device_id=device_id,
        source_type=category,
        search=search,
        limit=limit,
        offset=offset,
    )
    total = await asyncio.to_thread(
        rag_service.count_memories,
        device_id=device_id,
        source_type=category,
        search=search,
    )
    return {
        "items": items,
        "total": total,
        "limit": limit,
        "offset": offset,
        "device_id": device_id,
    }


@router.post("/item", summary="Create a long-term memory item")
async def create_memory_item(
    payload: MemoryCreateRequest,
    _key: str = Depends(verify_device_key),
) -> dict[str, Any]:
    try:
        chunk_id = await asyncio.to_thread(
            rag_service.add_memory,
            device_id=payload.device_id,
            content=payload.content,
            source_type=payload.category,
            source_ref="web_ui",
        )
        item = await asyncio.to_thread(rag_service.get_memory, chunk_id)
        return {"ok": True, "id": chunk_id, "item": item}
    except Exception as e:
        logger.error("Failed to create memory item: %r", e)
        raise HTTPException(status_code=500, detail=str(e))


@router.put("/item/{item_id}", summary="Update a long-term memory item")
async def update_memory_item(
    item_id: int,
    payload: MemoryUpdateRequest,
    _key: str = Depends(verify_device_key),
) -> dict[str, Any]:
    try:
        updated = await asyncio.to_thread(
            rag_service.update_memory,
            chunk_id=item_id,
            content=payload.content,
            source_type=payload.category,
        )
        if not updated:
            raise HTTPException(status_code=404, detail="Memory item not found")
        item = await asyncio.to_thread(rag_service.get_memory, item_id)
        return {"ok": True, "id": item_id, "item": item}
    except HTTPException:
        raise
    except Exception as e:
        logger.error("Failed to update memory item %d: %r", item_id, e)
        raise HTTPException(status_code=500, detail=str(e))


@router.delete("/item/{item_id}", summary="Delete a long-term memory item")
async def delete_memory_item(
    item_id: int,
    _key: str = Depends(verify_device_key),
) -> dict[str, Any]:
    deleted = await asyncio.to_thread(rag_service.delete_memory, item_id)
    if not deleted:
        raise HTTPException(status_code=404, detail="Memory item not found")
    return {"ok": True, "deleted": True, "id": item_id}


@router.post("/query", summary="Test semantic vector retrieval")
async def test_memory_query(
    payload: MemoryQueryRequest,
    _key: str = Depends(verify_device_key),
) -> dict[str, Any]:
    try:
        res = await asyncio.to_thread(
            rag_service.search,
            query=payload.query,
            device_id=payload.device_id,
            top_k=payload.top_k,
            threshold=payload.threshold,
        )
        return res
    except Exception as e:
        logger.error("Memory query failed: %r", e)
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/archive", summary="Archive daily notes from SPIFFS (FR-8)")
async def archive_daily_notes(
    payload: DailyNoteArchiveRequest,
    _key: str = Depends(verify_device_key),
) -> dict[str, Any]:
    try:
        count = await asyncio.to_thread(
            rag_service.archive_daily_note,
            device_id=payload.device_id,
            note_date=payload.note_date,
            raw_content=payload.content,
        )
        return {
            "ok": True,
            "archived_date": payload.note_date,
            "chunks_indexed": count,
            "device_id": payload.device_id,
        }
    except Exception as e:
        logger.error("Archive daily note failed: %r", e)
        raise HTTPException(status_code=500, detail=str(e))
