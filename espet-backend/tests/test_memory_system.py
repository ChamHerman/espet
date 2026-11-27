"""
Unit and integration tests for ESPET's decoupled action logging,
conflict-resolving memory upsert, distillation, and agent memory management tools.
"""

from __future__ import annotations

import tempfile
import pytest

from services.memory_db import MemoryDB
from services.rag_service import RAGService
from services.memory_organizer import MemoryOrganizer
from api.custom_tools import (
    execute_memory_save,
    execute_memory_search,
    execute_memory_update,
    execute_memory_forget,
)
import services.rag_service as rs_mod


@pytest.fixture
def temp_rag():
    with tempfile.TemporaryDirectory() as tmpdir:
        db_path = f"{tmpdir}/test_mem.db"
        db = MemoryDB(db_path=db_path)
        rag = RAGService(db=db)
        orig_rag = rs_mod.rag_service
        rs_mod.rag_service = rag
        yield rag
        rs_mod.rag_service = orig_rag
        db.close()


def test_action_logging_decoupled_from_vector(temp_rag):
    """Verify tool action traces go strictly to action_logs and never pollute vec_memories."""
    device_id = "espet_test_01"

    # Log several actions
    act_id1 = temp_rag.log_tool_action(device_id, "gpio_write", '{"pin":42,"state":1}', "OK")
    act_id2 = temp_rag.log_tool_action(device_id, "get_current_time", "{}", "2026-11-20T22:00:00")

    assert act_id1 > 0
    assert act_id2 > 0

    # Verify action_logs table has both records
    actions = temp_rag.list_recent_actions(device_id, limit=10)
    assert len(actions) == 2
    assert actions[0]["tool_name"] in ("gpio_write", "get_current_time")

    # Verify vec_memories has ZERO records
    cur = temp_rag.db.fetchone("SELECT COUNT(*) FROM vec_memories")
    assert cur[0] == 0

    # Verify memory_chunks has ZERO records
    chunks = temp_rag.list_memories(device_id)
    assert len(chunks) == 0


def test_delete_noisy_actions_purges_all_action_chunks(temp_rag):
    """Verify delete_noisy_actions removes legacy action chunks from vec_memories and memory_chunks."""
    device_id = "espet_test_02"

    # Add legacy action memory chunks
    temp_rag.add_memory(device_id, "Action: executed tool gpio_read with args {}", source_type="action")
    temp_rag.add_memory(device_id, "Action: executed tool read_file with args /spiffs/test", source_type="action")
    # Add a real preference memory
    fact_id = temp_rag.add_memory(device_id, "User prefers dark mode", source_type="preference")

    assert temp_rag.count_memories(device_id) == 3

    # Run cleanup
    cleaned = temp_rag.delete_noisy_actions(device_id, all_actions=True)
    assert cleaned == 2

    # Verify only the preference memory remains
    remaining = temp_rag.list_memories(device_id)
    assert len(remaining) == 1
    assert remaining[0]["id"] == fact_id
    assert remaining[0]["content"] == "User prefers dark mode"


def test_upsert_memory_with_conflict_resolution(temp_rag):
    """Verify conflict resolution: duplicate skipping and updating similar preferences."""
    device_id = "espet_test_03"

    # 1. First insert
    res1 = temp_rag.upsert_memory_with_conflict_resolution(
        device_id=device_id,
        content="User prefers green tea over coffee",
        source_type="preference",
    )
    assert res1["action"] == "created"
    chunk_id = res1["id"]

    # 2. Exact duplicate insert -> should skip
    res2 = temp_rag.upsert_memory_with_conflict_resolution(
        device_id=device_id,
        content="User prefers green tea over coffee",
        source_type="preference",
    )
    assert res2["action"] == "duplicate_skipped"
    assert res2["id"] == chunk_id

    # 3. Contradictory/updated preference -> should update in-place
    res3 = temp_rag.upsert_memory_with_conflict_resolution(
        device_id=device_id,
        content="User prefers black tea over coffee",
        source_type="preference",
    )
    assert res3["action"] == "updated"
    assert res3["id"] == chunk_id

    # Verify database has only 1 memory chunk with updated text
    memories = temp_rag.list_memories(device_id)
    assert len(memories) == 1
    assert memories[0]["content"] == "User prefers black tea over coffee"


def test_distill_working_memory(temp_rag):
    """Verify distill_working_memory builds bounded Markdown for on-device SPIFFS MEMORY.md."""
    device_id = "espet_test_04"

    temp_rag.add_memory(device_id, "User prefers metric units", source_type="preference")
    temp_rag.add_memory(device_id, "User works at TARUMT on FYP project", source_type="fact")
    temp_rag.add_memory(device_id, "Supervisor meeting on Friday at 3 PM", source_type="event")

    distilled = temp_rag.distill_working_memory(device_id, max_bytes=1000)

    assert "# Working Memory (Distilled)" in distilled
    assert "## User Preferences" in distilled
    assert "User prefers metric units" in distilled
    assert "## Important Facts" in distilled
    assert "User works at TARUMT on FYP project" in distilled
    assert "## Events & Tasks" in distilled
    assert "Supervisor meeting on Friday at 3 PM" in distilled
    assert len(distilled.encode("utf-8")) <= 1000


def test_custom_tools_memory_lifecycle(temp_rag):
    """Verify agent tools: memory_save, memory_search, memory_update, memory_forget."""
    import asyncio

    async def _run():
        device_id = "espet_test_05"

        # 1. Save
        save_res = await execute_memory_save({
            "device_id": device_id,
            "content": "User lives in Penang",
            "category": "fact",
        })
        assert "Successfully saved" in save_res["result"]

        # 2. Search
        search_res = await execute_memory_search({
            "device_id": device_id,
            "query": "Where does the user live?",
        })
        assert "Penang" in search_res["result"]

        # 3. Update
        update_res = await execute_memory_update({
            "device_id": device_id,
            "target": "Where does the user live?",
            "new_content": "User lives in Kuala Lumpur",
        })
        assert "Successfully updated" in update_res["result"]

        # 4. Search again to verify update
        search_res2 = await execute_memory_search({
            "device_id": device_id,
            "query": "Where does the user live?",
        })
        assert "Kuala Lumpur" in search_res2["result"]
        assert "Penang" not in search_res2["result"]

        # 5. Forget
        forget_res = await execute_memory_forget({
            "device_id": device_id,
            "query": "User lives in Kuala Lumpur",
        })
        assert "Successfully forgot" in forget_res["result"]

        # 6. Verify gone
        search_res3 = await execute_memory_search({
            "device_id": device_id,
            "query": "Where does the user live?",
        })
        assert "No relevant" in search_res3["result"]

    asyncio.run(_run())


def test_memory_organizer_should_skip():
    """Verify should_skip correctly filters out trivial chatter and greetings."""
    assert MemoryOrganizer.should_skip("") is True
    assert MemoryOrganizer.should_skip("   ") is True
    assert MemoryOrganizer.should_skip("turn on led") is True
    assert MemoryOrganizer.should_skip("hi") is True
    assert MemoryOrganizer.should_skip("hello") is True
    assert MemoryOrganizer.should_skip("thanks") is True
    assert MemoryOrganizer.should_skip("what time is it") is True

    # Informative utterances should NOT be skipped
    assert MemoryOrganizer.should_skip("My name is Cham Herman and I am a student") is False
    assert MemoryOrganizer.should_skip("I am allergic to peanuts") is False
    assert MemoryOrganizer.should_skip("My FYP supervisor is Dr. Wong") is False
