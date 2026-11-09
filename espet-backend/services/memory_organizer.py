"""
Memory Organizer Service — Autonomous extraction of user profile updates & semantic memories.
"""

from __future__ import annotations

import asyncio
import json
import logging
import re
from typing import Any

import httpx

import config_store as store
from services.gateway_settings import gateway_settings
from services.rag_service import rag_service

logger = logging.getLogger("espet-backend.memory_organizer")


class MemoryOrganizer:
    """
    Parses conversational turns between user and agent to proactively extract:
    1. User profile updates (Name, Language, Timezone, User notes) -> synced to USER.md.
    2. Episodic semantic memories -> stored in SQLite-Vec RAG.
    """

    @staticmethod
    def strip_markdown_json(text: str) -> str:
        """
        Strips markdown code fences (```json ... ```) or finds the outermost JSON object.
        """
        t = text.strip()
        # Direct fence extraction
        fence_match = re.search(r"```(?:json)?\s*([\s\S]*?)\s*```", t, re.IGNORECASE)
        if fence_match:
            return fence_match.group(1).strip()

        # Find first '{' and last '}'
        start = t.find("{")
        end = t.rfind("}")
        if start != -1 and end != -1 and end > start:
            return t[start : end + 1].strip()

        return t

    @classmethod
    def should_skip(cls, user_text: str) -> bool:
        clean = user_text.strip()
        if not clean:
            return True
        # Pure punctuation
        if not re.search(r"\w", clean):
            return True
        # Pure trivial hardware/status commands
        lower = clean.lower()
        trivial_commands = {
            "turn on led",
            "turn off led",
            "on led",
            "off led",
            "led on",
            "led off",
            "open led",
            "close led",
            "time",
            "current time",
            "what time is it",
            "ping",
        }
        if lower in trivial_commands:
            return True
        return False

    async def extract_from_turn(
        self,
        device_id: str,
        user_text: str,
        assistant_json_or_text: str,
    ) -> dict[str, Any]:
        """
        Analyzes a conversation turn to extract profile updates and long-term memories.
        """
        if self.should_skip(user_text):
            logger.debug("MemoryOrganizer: skipped turn for query '%s'", user_text[:40])
            return {"skipped": True}

        # Safely extract assistant content string
        assistant_text = ""
        try:
            parsed = json.loads(assistant_json_or_text)
            if isinstance(parsed, dict):
                choices = parsed.get("choices", [])
                if choices and isinstance(choices[0], dict):
                    msg = choices[0].get("message", {})
                    assistant_text = msg.get("content") or ""
        except Exception:
            assistant_text = assistant_json_or_text.strip()

        cfg = gateway_settings.llm_config()
        if not cfg.get("base_url"):
            return {"error": "LLM base_url not configured"}

        system_prompt = (
            "You are an autonomous memory organizer for an embodied personal AI assistant (ESPET).\n"
            "Analyze the conversation turn between the User and the Assistant.\n"
            "Extract any durable user profile information or personal facts/preferences/events worth remembering long-term.\n"
            "Return ONLY a valid JSON object with the following schema, with no markdown fences, no conversational filler:\n"
            "{\n"
            '  "profile_update": {\n'
            '    "name": "string or null",\n'
            '    "language": "string or null",\n'
            '    "timezone": "string or null",\n'
            '    "note": "string or null (e.g. background info, current occupation, interests)"\n'
            "  },\n"
            '  "memories": [\n'
            "    {\n"
            '      "content": "string (clear self-contained natural language fact)",\n'
            '      "category": "fact" | "preference" | "event" | "note"\n'
            "    }\n"
            "  ]\n"
            "}\n"
            "If nothing new or durable is mentioned, set profile_update to null and memories to [].\n"
            "Preserve user introductions ('im Cham Herman' -> name: 'Cham Herman')."
        )

        turn_content = (
            f"User: {user_text}\n"
            f"Assistant: {assistant_text}\n"
        )

        url = cfg["base_url"].rstrip("/") + "/chat/completions"
        body = {
            "model": cfg.get("model") or "gpt-4o-mini",
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": turn_content},
            ],
            "temperature": 0.0,
            "max_tokens": 400,
            "stream": False,
        }

        try:
            async with httpx.AsyncClient(timeout=30) as client:
                resp = await client.post(
                    url,
                    json=body,
                    headers={
                        "Authorization": f"Bearer {cfg['api_key']}",
                        "Accept": "application/json",
                    },
                )

            if resp.status_code != 200:
                logger.warning("MemoryOrganizer LLM call failed with HTTP %d: %s", resp.status_code, resp.text[:200])
                return {"error": f"HTTP {resp.status_code}"}

            raw_reply = ""
            try:
                reply_json = resp.json()
                raw_reply = reply_json.get("choices", [{}])[0].get("message", {}).get("content", "")
            except Exception:
                raw_reply = resp.text

            clean_json_str = self.strip_markdown_json(raw_reply)
            parsed_data = json.loads(clean_json_str)
        except Exception as e:
            logger.warning("MemoryOrganizer failed to parse extraction JSON: %r (raw: %.150s)", e, raw_reply)
            return {"error": str(e)}

        profile_update = parsed_data.get("profile_update")
        if profile_update and isinstance(profile_update, dict):
            name = profile_update.get("name")
            lang = profile_update.get("language")
            tz = profile_update.get("timezone")
            note = profile_update.get("note")

            if any((name, lang, tz, note)):
                changed = store.patch_user_profile(
                    device_id=device_id,
                    name=name,
                    language=lang,
                    timezone=tz,
                    note=note,
                )
                if changed:
                    logger.info("MemoryOrganizer patched profile for %s (name=%s, note=%s)", device_id, name, note)
                    # Delayed import to avoid circular dependency
                    from api.cloud_router import notify_device_sync

                    await notify_device_sync(device_id)

        memories = parsed_data.get("memories") or []
        added_memories = []
        for mem in memories:
            if not isinstance(mem, dict):
                continue
            content = (mem.get("content") or "").strip()
            cat = mem.get("category") or "fact"
            if not content:
                continue

            # Check for existing memory with distance threshold 0.15 (cosine similarity >= 0.85)
            try:
                res = await asyncio.to_thread(
                    rag_service.search,
                    query=content,
                    device_id=device_id,
                    top_k=1,
                    threshold=0.15,
                )
                if not res.get("results"):
                    new_id = await asyncio.to_thread(
                        rag_service.add_memory,
                        device_id=device_id,
                        content=content,
                        source_type=cat,
                        source_ref="auto_organizer",
                    )
                    added_memories.append({"id": new_id, "content": content})
                    logger.info("MemoryOrganizer added new %s memory for %s: %s", cat, device_id, content[:60])
                else:
                    logger.debug("MemoryOrganizer: skipped duplicate memory: %s", content[:40])
            except Exception as e:
                logger.warning("MemoryOrganizer failed to store memory '%s': %r", content[:40], e)

        return {"profile_updated": bool(profile_update), "added_memories": added_memories}


memory_organizer = MemoryOrganizer()
