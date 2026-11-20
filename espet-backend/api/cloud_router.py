"""
Cloud Router — hybrid-mode device WebSocket endpoint + binding REST.

/ws/device : the ESP32's persistent outbound connection (device = client).
             Auth via ?key=<DEVICE_API_KEY>. First frame must be `hello`.
             Routed frames (services/telegram_gateway.py) are pushed here;
             device frames (message_out / photo_out / ping) are handled here.

REST       : binding inspection/management, guarded by the device key.
"""

from __future__ import annotations

import asyncio
import json
import logging
import re
import time
from pathlib import Path
from typing import Any

import httpx

from fastapi import APIRouter, Depends, Query, WebSocket, WebSocketDisconnect

from auth.device_auth import verify_device_key
from config import settings
from services.device_registry import DeviceRegistry
from services.gateway_settings import gateway_settings
from services.telegram_gateway import TelegramGateway, get_gateway, set_gateway
from services.rag_service import rag_service
from services.memory_organizer import memory_organizer
import config_store as store
logger = logging.getLogger("espet-backend.cloud")

router = APIRouter(prefix="/api/cloud", tags=["cloud"])

registry = DeviceRegistry()

# Asynchronous response tracking for manual recording and voice listen toggle
_pending_record_start: dict[str, asyncio.Future] = {}
_pending_record_stop: dict[str, asyncio.Future] = {}
_pending_voice_listen: dict[str, asyncio.Future] = {}
_voice_listen_state: dict[str, bool] = {}

# ---------------------------------------------------------------------------
# Web Chat clients and history storage (channel: cloud_link, chat_id: dashboard)
# ---------------------------------------------------------------------------
_web_chat_clients: set[WebSocket] = set()
_HISTORY_PATH = Path("data/web_chat_history.json")


def _load_chat_history() -> list[dict]:
    try:
        if _HISTORY_PATH.exists():
            return json.loads(_HISTORY_PATH.read_text(encoding="utf-8"))
    except Exception as e:
        logger.warning("Failed to load chat history: %s", e)
    return []


def _save_chat_history(history: list[dict]) -> None:
    try:
        _HISTORY_PATH.parent.mkdir(parents=True, exist_ok=True)
        _HISTORY_PATH.write_text(
            json.dumps(history[-80:], ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
    except Exception as e:
        logger.warning("Failed to save chat history: %s", e)


_web_chat_history: list[dict] = _load_chat_history()


def _notify_web_chat_device_status(online: bool, device_id: str = "") -> None:
    if not _web_chat_clients:
        return
    frame = json.dumps({
        "type": "device_status",
        "online": online,
        "device_id": device_id,
        "devices": registry.connected_devices(),
    })
    for ws in list(_web_chat_clients):
        try:
            asyncio.create_task(ws.send_text(frame))
        except Exception:
            pass


_last_photo_timestamp: float = 0.0
_last_photo_consumed: bool = True


def record_new_photo() -> None:
    """Flag that a new camera photo has been captured/uploaded."""
    global _last_photo_timestamp, _last_photo_consumed
    _last_photo_timestamp = time.time()
    _last_photo_consumed = False


def pop_recent_photo_url() -> str | None:
    """Consume and return the URL of a recently captured photo if available within 45s."""
    global _last_photo_consumed
    if not _last_photo_consumed and (time.time() - _last_photo_timestamp) < 45.0:
        _last_photo_consumed = True
        return f"/api/camera/last-photo?t={int(_last_photo_timestamp * 1000)}"
    return None


async def _broadcast_to_web_chat(chat_id: str, text: str, image: str | None = None) -> None:
    if not _web_chat_clients:
        return
    if text == "⏳ Thinking...":
        frame = json.dumps({"type": "status", "status": "thinking"})
    else:
        img_url = image or pop_recent_photo_url()
        msg: dict[str, Any] = {
            "id": f"{int(time.time() * 1000)}-agent",
            "role": "agent",
            "text": text,
            "ts": int(time.time() * 1000),
        }
        if img_url:
            msg["image"] = img_url

        _web_chat_history.append(msg)
        if len(_web_chat_history) > 80:
            _web_chat_history.pop(0)
        _save_chat_history(_web_chat_history)
        frame = json.dumps({
            "type": "response",
            "content": text,
            "message": msg,
            "image": img_url,
            "chat_id": chat_id,
        })

    dead = []
    for ws in list(_web_chat_clients):
        try:
            await ws.send_text(frame)
        except Exception:
            dead.append(ws)
    for ws in dead:
        _web_chat_clients.discard(ws)



def _build_gateway() -> TelegramGateway | None:
    """(Re)create the Telegram poller from the current gateway settings.
    Returns None when disabled or the token is missing."""
    snap = gateway_settings.get()
    if not snap.get("cloud_gateway_enabled") or not snap.get("telegram_bot_token"):
        return None
    return TelegramGateway(
        registry,
        bot_token=str(snap["telegram_bot_token"]),
        api_base=settings.telegram_api_base,
        poll_interval_s=float(snap.get("telegram_poll_interval_s") or 0.0),
        long_poll_s=30,
        offset_path="data/telegram_offset.txt",
    )


def restart_gateway() -> dict:
    """Apply gateway settings: stop the current poller and start a new one
    when enabled. Safe to call repeatedly."""
    old = get_gateway()
    if old:
        try:
            loop = asyncio.get_running_loop()
            loop.create_task(old.stop())
        except RuntimeError:
            pass  # no loop (shutdown path) — task dies with the loop
        set_gateway(None)
    gw = _build_gateway()
    if gw:
        set_gateway(gw)
        gw.start()
        logger.info("Telegram gateway restarted (token changed/enabled)")
    else:
        logger.info("Telegram gateway stopped (disabled or no token)")
    return {"running": gw is not None}
async def notify_device_sync(device_id: str) -> bool:
    """
    Sends sync_now frame to connected device WebSocket and broadcasts
    config_updated event to all active web dashboard clients.
    Returns True if sent to device, False if device is offline.
    """
    # 1. Broadcast config_updated frame to all connected Web UI clients
    try:
        cfg = store.get_config(device_id)
        versions = cfg.get("versions", {})
        update_msg = json.dumps({
            "type": "config_updated",
            "device_id": device_id,
            "versions": versions
        })
        for client_ws in list(_web_chat_clients):
            try:
                await client_ws.send_text(update_msg)
            except Exception:
                _web_chat_clients.discard(client_ws)
    except Exception as e:
        logger.warning("Failed to broadcast config_updated: %s", e)

    # 2. Push sync_now frame to connected physical device
    with registry._lock:
        ws = registry.connections.get(device_id)
    if ws:
        try:
            await ws.send_text(json.dumps({"type": "sync_now", "device_id": device_id}))
            logger.info("Pushed sync_now frame to connected device %s", device_id)
            return True
        except Exception as e:
            logger.warning("Failed to push sync_now to device %s: %s", device_id, e)
            return False
    return False



@router.websocket("/ws/device")
async def ws_device(websocket: WebSocket, key: str = Query("")):
    # --- auth ----------------------------------------------------------------
    if not settings.device_api_key or key != settings.device_api_key:
        await websocket.accept()
        await websocket.close(code=4401)
        logger.warning("Device WS rejected: bad or missing device key")
        return

    await websocket.accept()

    # --- handshake: first frame must be hello -------------------------------
    raw = await websocket.receive_text()
    try:
        hello = json.loads(raw)
    except json.JSONDecodeError:
        await websocket.send_text(json.dumps(
            {"type": "error", "detail": "first frame must be hello JSON"}))
        await websocket.close(code=4402)
        return
    device_id = str(hello.get("device_id") or "").strip()
    if not device_id:
        await websocket.send_text(json.dumps(
            {"type": "error", "detail": "hello requires device_id"}))
        await websocket.close(code=4402)
        return

    client_ip = websocket.client.host if websocket.client else ""
    registry.register_device(device_id, ip=client_ip, fw=str(hello.get("fw", "")))
    registry.register_connection(device_id, websocket)
    primary_chat = registry.primary_chat_for_device(device_id) or ""
    cur_epoch = int(time.time())
    dev_cfg = store.get_config(device_id)
    dev_tz = dev_cfg.get("device", {}).get("timezone", "UTC")
    await websocket.send_text(json.dumps({
        "type": "hello_ack",
        "device_id": device_id,
        "chat_id": primary_chat,
        "server_epoch": cur_epoch,
        "tz": dev_tz,
    }))
    logger.info("Device %s connected (fw=%s) — %d device(s) online",
                device_id, hello.get("fw", "?"), len(registry.connected_devices()))
    _notify_web_chat_device_status(True, device_id)

    # --- frame loop -----------------------------------------------------------
    try:
        while True:
            raw = await websocket.receive_text()
            try:
                frame = json.loads(raw)
            except json.JSONDecodeError:
                await websocket.send_text(json.dumps(
                    {"type": "error", "detail": "frame is not valid JSON"}))
                continue

            ftype = frame.get("type")

            if ftype == "message_out":
                chat_id = str(frame.get("chat_id", ""))
                text = str(frame.get("text", ""))
                if not chat_id or not text:
                    await websocket.send_text(json.dumps(
                        {"type": "error", "detail": "message_out needs chat_id+text"}))
                    continue

                if chat_id == "dashboard" or chat_id.startswith("ws_"):
                    asyncio.create_task(_broadcast_to_web_chat(chat_id, text))
                    continue

                gw = get_gateway()
                if gw is None:
                    await websocket.send_text(json.dumps(
                        {"type": "error", "detail": "gateway not configured"}))
                    continue
                # Fire-and-forget so the receive loop keeps reading frames
                asyncio.create_task(_send_logged(gw, chat_id, text))

            elif ftype == "photo_out":
                import base64
                chat_id = str(frame.get("chat_id", ""))
                caption = str(frame.get("caption", ""))
                try:
                    image = base64.b64decode(frame.get("image_b64", ""))
                except Exception:
                    await websocket.send_text(json.dumps(
                        {"type": "error", "detail": "photo_out image_b64 invalid"}))
                    continue

                last_photo_path = Path(__file__).parent.parent / "data" / "last_photo.jpg"
                last_photo_path.parent.mkdir(parents=True, exist_ok=True)
                last_photo_path.write_bytes(image)
                record_new_photo()

                gw = get_gateway()
                if gw and chat_id and chat_id != "dashboard":
                    asyncio.create_task(_photo_logged(gw, chat_id, image, caption))

                if chat_id == "dashboard" or not gw:
                    asyncio.create_task(_broadcast_to_web_chat(chat_id, caption or "📸 Photo captured"))

            elif ftype == "llm.request":
                request_id = frame.get("request_id")
                body_str = frame.get("body")
                if not isinstance(body_str, str) or not body_str:
                    await websocket.send_text(json.dumps(
                        {"type": "llm.response", "request_id": request_id,
                         "ok": False, "status": 0,
                         "body": json.dumps({"error": "empty llm body"})}))
                    continue
                asyncio.create_task(_llm_relay(websocket, request_id, body_str, device_id=device_id))

            elif ftype == "ping":
                await websocket.send_text(json.dumps({"type": "pong"}))

            elif ftype == "record_status":
                fut = _pending_record_start.pop(device_id, None)
                if fut and not fut.done():
                    fut.set_result(frame)

            elif ftype == "record_result":
                fut = _pending_record_stop.pop(device_id, None)
                if fut and not fut.done():
                    fut.set_result(frame)

            elif ftype == "voice_listen_status":
                _voice_listen_state[device_id] = frame.get("enabled", False)
                fut = _pending_voice_listen.pop(device_id, None)
                if fut and not fut.done():
                    fut.set_result(frame)
            elif ftype == "camera_stream_status":
                logger.info("Camera stream status from %s: %s", device_id, frame)
            elif ftype == "device_sync_report":
                memory_md = frame.get("memory_md")
                jobs = frame.get("jobs")
                store.apply_device_report(device_id, memory_md, jobs)
                logger.info("Received device_sync_report from %s (memory_md %d bytes, %d jobs)",
                            device_id, len(memory_md or ""), len(jobs or []))

            elif ftype == "device_memory_update":
                store.update_markdown("memory", frame.get("content", ""), device_id)

            elif ftype == "device_jobs_update":
                store.apply_device_report(device_id, memory_md=None, jobs=frame.get("jobs", []))


            else:
                await websocket.send_text(json.dumps(
                    {"type": "error", "detail": f"unknown frame type {ftype!r}"}))

    except WebSocketDisconnect:
        logger.info("Device %s disconnected", device_id)
    finally:
        for pmap in (_pending_record_start, _pending_record_stop, _pending_voice_listen):
            fut = pmap.pop(device_id, None)
            if fut and not fut.done():
                fut.set_exception(RuntimeError("Device disconnected"))
        registry.remove_connection(device_id, websocket)
        registry.mark_offline(device_id)
        logger.info("Device %s removed — %d device(s) online",
                    device_id, len(registry.connected_devices()))
        _notify_web_chat_device_status(len(registry.connected_devices()) > 0)


# ---------------------------------------------------------------------------
# Web Chat WebSocket Relay endpoint
# ---------------------------------------------------------------------------

@router.websocket("/ws/chat")
async def ws_chat(websocket: WebSocket):
    await websocket.accept()
    _web_chat_clients.add(websocket)
    try:
        online_devices = registry.connected_devices()
        await websocket.send_text(json.dumps({
            "type": "init",
            "online": len(online_devices) > 0,
            "devices": online_devices,
            "history": _web_chat_history,
        }))

        while True:
            raw = await websocket.receive_text()
            try:
                data = json.loads(raw)
            except json.JSONDecodeError:
                continue

            action = data.get("type")
            if action == "clear":
                _web_chat_history.clear()
                _save_chat_history(_web_chat_history)
                devs = registry.connected_devices()
                if devs:
                    dev_id = devs[0]
                    _, dws = registry.get_connection(dev_id)
                    if dws:
                        try:
                            await dws.send_text(json.dumps({
                                "type": "message_in",
                                "chat_id": "dashboard",
                                "text": "/clear",
                            }))
                        except Exception:
                            pass
                await websocket.send_text(json.dumps({"type": "cleared"}))

            elif action == "message":
                text = str(data.get("content") or "").strip()
                if not text:
                    continue
                user_msg = {
                    "id": f"{int(time.time() * 1000)}-user",
                    "role": "user",
                    "text": text,
                    "ts": int(time.time() * 1000),
                }
                _web_chat_history.append(user_msg)
                if len(_web_chat_history) > 80:
                    _web_chat_history.pop(0)
                _save_chat_history(_web_chat_history)

                # Broadcast user message to other open browser tabs
                for ws in list(_web_chat_clients):
                    if ws != websocket:
                        try:
                            await ws.send_text(json.dumps({
                                "type": "message",
                                "message": user_msg,
                            }))
                        except Exception:
                            pass

                # Forward to ESP32 device
                devs = registry.connected_devices()
                if not devs:
                    sys_msg = {
                        "id": f"{int(time.time() * 1000)}-sys",
                        "role": "system",
                        "text": "⚠️ ESP32 设备当前未连接。请确保设备已开机并已连接到 Wi-Fi。",
                        "ts": int(time.time() * 1000),
                    }
                    await websocket.send_text(json.dumps({
                        "type": "response",
                        "content": sys_msg["text"],
                        "message": sys_msg,
                    }))
                    continue

                dev_id = devs[0]
                _, dws = registry.get_connection(dev_id)
                if dws is None:
                    await websocket.send_text(json.dumps({
                        "type": "response",
                        "content": "⚠️ 设备连接异常断开。",
                        "role": "system",
                    }))
                    continue

                try:
                    await dws.send_text(json.dumps({
                        "type": "message_in",
                        "chat_id": "dashboard",
                        "text": text,
                    }))
                except Exception as e:
                    logger.warning("Failed to forward chat to device %s: %s", dev_id, e)
                    await websocket.send_text(json.dumps({
                        "type": "response",
                        "content": f"⚠️ 发送失败: {e}",
                        "role": "system",
                    }))
    except WebSocketDisconnect:
        pass
    finally:
        _web_chat_clients.discard(websocket)



async def _send_logged(gateway, chat_id: str, text: str) -> None:
    try:
        ok = await gateway.send_text(chat_id, text)
        logger.info("Device reply -> Telegram chat %s: %s (%.60s)",
                    chat_id, "sent" if ok else "FAILED", text)
    except Exception as e:
        logger.warning("sendMessage for chat %s raised: %r", chat_id, e)


async def _photo_logged(gateway, chat_id: str, image: bytes, caption: str) -> None:
    try:
        ok = await gateway.send_photo(chat_id, image, caption)
        logger.info("Device photo -> Telegram chat %s: %s (%d bytes)",
                    chat_id, "sent" if ok else "FAILED", len(image))
    except Exception as e:
        logger.warning("sendPhoto for chat %s raised: %r", chat_id, e)


# ---------------------------------------------------------------------------
# LLM relay — the device's agent builds the request body; the cloud adds its
# own API key (never shipped to the device) and calls the OpenAI-compatible
# provider, returning the raw provider response to the device.
# ---------------------------------------------------------------------------

async def _llm_relay(websocket, request_id, body_str: str, device_id: str = "espet_s3_01") -> None:
    cfg = gateway_settings.llm_config()

    async def _reply(ok: bool, status: int, body_text: str) -> None:
        try:
            await websocket.send_text(json.dumps({
                "type": "llm.response", "request_id": request_id,
                "ok": ok, "status": status, "body": body_text,
            }))
        except Exception as e:
            logger.warning("llm.response push failed: %r", e)

    if not cfg["api_key"] or not cfg["base_url"]:
        await _reply(False, 0, json.dumps(
            {"error": "LLM key/base URL not configured on server — set it in "
                      "the dashboard (System > Cloud Gateway)"}))
        return

    try:
        body = json.loads(body_str)
    except json.JSONDecodeError as e:
        await _reply(False, 0, json.dumps({"error": f"bad llm body: {e}"}))
        return

    if isinstance(body, dict):
        body.setdefault("stream", False)
        if cfg["model"]:
            body["model"] = cfg["model"]   # server-side model override

        # --- RAG Injection ---
        messages = body.get("messages")
        if isinstance(messages, list) and messages:
            user_query = ""
            for msg in reversed(messages):
                if isinstance(msg, dict) and msg.get("role") == "user":
                    c = msg.get("content")
                    if isinstance(c, str):
                        user_query = c.strip()
                    elif isinstance(c, list):
                        parts = [p.get("text", "") for p in c if isinstance(p, dict) and p.get("type") == "text"]
                        user_query = " ".join(parts).strip()
                    if user_query:
                        break

            if user_query:
                # Fast heuristic context reformulation: resolve pronouns or short follow-ups from prior turn
                search_query = user_query
                pronouns = {"he", "she", "it", "they", "that", "this", "his", "her", "him", "them", "there", "then"}
                words = set(re.findall(r"\b\w+\b", user_query.lower()))
                if words & pronouns or len(words) <= 3:
                    context_clues = []
                    for prev_msg in reversed(messages[:-1]):
                        if isinstance(prev_msg, dict):
                            prev_text = prev_msg.get("content") or ""
                            if isinstance(prev_text, str) and prev_text.strip():
                                clues = [w for w in re.findall(r"\b[A-Z][a-z0-9_]+\b", prev_text) if len(w) > 2]
                                context_clues.extend(clues[:3])
                                if len(context_clues) >= 3:
                                    break
                    if context_clues:
                        search_query = f"{' '.join(context_clues[:3])} {user_query}"

                try:
                    rag_res = await asyncio.to_thread(
                        rag_service.search,
                        query=search_query,
                        device_id=device_id,
                        top_k=settings.rag_top_k,
                        threshold=settings.rag_threshold,
                        exclude_types=["action"],
                    )
                    if rag_res.get("results"):
                        lines = [
                            "## Established User Context (Priority: Follow Strictly)",
                            "The following verified facts, preferences, and commitments must guide your response:",
                        ]
                        for item in rag_res["results"]:
                            lines.append(f"- [{item['source_type']}]: {item['content']}")
                        injected_block = "\n".join(lines)

                        if messages and isinstance(messages[0], dict) and messages[0].get("role") == "system":
                            orig_content = messages[0].get("content") or ""
                            messages[0]["content"] = f"{orig_content}\n\n{injected_block}".strip()
                        else:
                            messages.insert(0, {"role": "system", "content": injected_block})

                        logger.info(
                            "rag: Injected %d memory chunk(s) for device %s (%.2f ms, query='%s')",
                            len(rag_res["results"]),
                            device_id,
                            rag_res.get("latency_ms", 0.0),
                            search_query[:40],
                        )
                except Exception as ex:
                    logger.warning("rag: search/injection failed: %r", ex)
    used_model = body.get("model") if isinstance(body, dict) else None

    url = cfg["base_url"].rstrip("/") + "/chat/completions"
    try:
        async with httpx.AsyncClient(timeout=90) as client:
            resp = await client.post(
                url, json=body,
                headers={"Authorization": f"Bearer {cfg['api_key']}", "Accept": "application/json"})
        logger.info("LLM relay: %s model=%s -> HTTP %d (%d bytes)",
                    url, used_model, resp.status_code, len(resp.text))
        if resp.status_code != 200 or len(resp.text) < 400:
            logger.info("LLM relay payload preview: %.300s", resp.text)
        await _reply(resp.status_code == 200, resp.status_code, resp.text)
        if resp.status_code == 200:
            asyncio.create_task(_capture_episodic_action(device_id, resp.text))
            if user_query:
                async def _safe_extract():
                    try:
                        await memory_organizer.extract_from_turn(device_id, user_query, resp.text)
                    except Exception as ex:
                        logger.warning("MemoryOrganizer extraction task failed for %s: %r", device_id, ex)
                asyncio.create_task(_safe_extract())
    except Exception as e:
        logger.warning("LLM relay to %s failed: %r", url, e)
        await _reply(False, 0, json.dumps({"error": f"{type(e).__name__}: {e}"}))


HIGH_SALIENCE_ACTIONS = {
    "gmail_send_email",
    "gcal_create_event",
    "cron_add",
}

def _format_action_summary(tool_name: str, args_raw: str | dict) -> str | None:
    try:
        args = json.loads(args_raw) if isinstance(args_raw, str) else (args_raw or {})
    except Exception:
        args = {}
    if tool_name == "gmail_send_email":
        to = args.get("to") or args.get("recipient") or ""
        subj = args.get("subject") or "Message"
        return f"Sent email to {to} regarding '{subj}'"
    elif tool_name == "gcal_create_event":
        summary = args.get("summary") or args.get("title") or "Event"
        start = args.get("start_time") or args.get("start") or ""
        return f"Scheduled calendar event '{summary}' at {start}".strip()
    elif tool_name == "cron_add":
        desc = args.get("description") or args.get("command") or args.get("job") or ""
        cron_expr = args.get("cron") or args.get("schedule") or ""
        return f"Scheduled recurring task '{desc}' with schedule '{cron_expr}'".strip()
    return None

async def _capture_episodic_action(device_id: str, resp_text: str) -> None:
    try:
        data = json.loads(resp_text)
        choices = data.get("choices", [])
        if not choices:
            return
        msg = choices[0].get("message", {})
        tool_calls = msg.get("tool_calls", [])
        for tc in tool_calls:
            fn = tc.get("function", {})
            name = fn.get("name", "")
            args = fn.get("arguments", "")
            if name:
                # 1. Always record in lightweight rolling action_logs table (decoupled from vector RAG)
                await asyncio.to_thread(
                    rag_service.log_tool_action,
                    device_id=device_id,
                    tool_name=name,
                    arguments_json=args,
                    result_summary="",
                )
                # 2. Only promote high-salience external actions to semantic event memory in vector RAG
                if name in HIGH_SALIENCE_ACTIONS:
                    summary = _format_action_summary(name, args)
                    if summary:
                        await asyncio.to_thread(
                            rag_service.upsert_memory_with_conflict_resolution,
                            device_id=device_id,
                            content=summary,
                            source_type="event",
                            source_ref="relay_action",
                            threshold=0.20,
                        )
    except Exception as e:
        logger.debug("Episodic action capture skipped: %r", e)

# ---------------------------------------------------------------------------
# REST — bindings & connected devices (device-key guarded)
# ---------------------------------------------------------------------------

@router.get("/bindings")
async def list_bindings(_key: str = Depends(verify_device_key)):
    return {"bindings": registry.bindings()}


@router.post("/bind")
async def bind_chat(payload: dict | None = None,
                    chat_id: str | None = Query(None),
                    device_id: str | None = Query(None),
                    _key: str = Depends(verify_device_key)):
    data = payload or {}
    cid = data.get("chat_id") or chat_id
    did = data.get("device_id") or device_id
    if not cid or not did:
        return {"ok": False, "error": "chat_id and device_id are required"}
    registry.bind(str(cid), str(did))
    _, ws = registry.get_connection(str(did))
    if ws is not None:
        try:
            await ws.send_text(json.dumps({
                "type": "bind_sync",
                "chat_id": str(cid),
                "device_id": str(did),
            }))
        except Exception as e:
            logger.warning("Could not push bind_sync to %s: %s", did, e)
    return {"ok": True, "chat_id": str(cid), "device_id": str(did)}


@router.post("/unbind")
async def unbind_chat(payload: dict | None = None,
                      chat_id: str | None = Query(None),
                      _key: str = Depends(verify_device_key)):
    data = payload or {}
    cid = data.get("chat_id") or chat_id
    if not cid:
        return {"ok": False, "error": "chat_id is required"}
    removed = registry.unbind(str(cid))
    return {"ok": removed}


@router.get("/devices")
async def list_devices(_key: str = Depends(verify_device_key)):
    return {"connected": registry.connected_devices()}


# ---------------------------------------------------------------------------
# Diagnostics — live verification of Telegram Bot & LLM credentials
# ---------------------------------------------------------------------------

@router.post("/test-telegram")
async def test_telegram(payload: dict | None = None,
                        _key: str = Depends(verify_device_key)):
    """Validates the provided bot token (or current setting) with getMe."""
    token = (payload or {}).get("token") or gateway_settings.telegram_token()
    if not token:
        return {"ok": False, "error": "No Telegram bot token provided"}
    url = f"{settings.telegram_api_base.rstrip('/')}/bot{token}/getMe"
    try:
        async with httpx.AsyncClient(timeout=10) as client:
            resp = await client.get(url)
            try:
                data = resp.json()
            except (json.JSONDecodeError, ValueError):
                data = {}
            if resp.status_code == 200 and data.get("ok"):
                res = data.get("result", {})
                return {
                    "ok": True,
                    "bot": {
                        "id": res.get("id"),
                        "username": res.get("username"),
                        "first_name": res.get("first_name"),
                        "can_join_groups": res.get("can_join_groups"),
                    },
                }
            return {"ok": False, "error": data.get("description", resp.text[:200].strip() or f"HTTP {resp.status_code}")}
    except Exception as e:
        return {"ok": False, "error": f"{type(e).__name__}: {e}"}


@router.post("/test-llm")
async def test_llm(payload: dict | None = None,
                   _key: str = Depends(verify_device_key)):
    """Sends a minimal test completion to the LLM endpoint."""
    cfg = gateway_settings.llm_config()
    p = payload or {}
    api_key = p.get("api_key") or cfg["api_key"]
    base_url = (p.get("base_url") or cfg["base_url"] or "").rstrip("/")
    model = p.get("model") or cfg["model"] or "gpt-4o-mini"

    if not api_key or not base_url:
        return {"ok": False, "error": "Missing LLM API key or Base URL"}

    url = f"{base_url}/chat/completions"
    body = {
        "model": model,
        "messages": [{"role": "user", "content": "ping"}],
        "max_tokens": 10,
        "stream": False,
    }
    try:
        async with httpx.AsyncClient(timeout=15) as client:
            resp = await client.post(
                url, json=body,
                headers={"Authorization": f"Bearer {api_key}", "Accept": "application/json"})
            if resp.status_code == 200:
                try:
                    data = resp.json()
                    content = data.get("choices", [{}])[0].get("message", {}).get("content", "")
                    return {"ok": True, "model": model, "reply": (content or "").strip()}
                except (json.JSONDecodeError, ValueError):
                    pass

                text = resp.text
                chunks: list[str] = []
                for line in text.splitlines():
                    sline = line.strip()
                    if sline.startswith("data:") and not sline.startswith("data: [DONE]"):
                        try:
                            chunk = json.loads(sline[5:].strip())
                            delta = chunk.get("choices", [{}])[0].get("delta", {}).get("content", "")
                            if delta:
                                chunks.append(delta)
                        except Exception:
                            pass
                if chunks:
                    return {"ok": True, "model": model, "reply": "".join(chunks).strip()}
                return {"ok": False, "status": resp.status_code, "error": f"Invalid response: {text[:200].strip() or 'empty body'}"}
            return {"ok": False, "status": resp.status_code, "error": resp.text[:300].strip() or f"HTTP {resp.status_code}"}
    except Exception as e:
        return {"ok": False, "error": f"{type(e).__name__}: {e}"}


# ---------------------------------------------------------------------------
# Gateway settings (Telegram bot + LLM credentials) — dashboard-editable
# ---------------------------------------------------------------------------

@router.get("/gateway-config")
async def get_gateway_config(_key: str = Depends(verify_device_key)):
    snap = gateway_settings.get()
    gw = get_gateway()
    snap = dict(snap)
    snap["poller_running"] = gw is not None
    return snap


@router.put("/gateway-config")
async def put_gateway_config(payload: dict,
                             _key: str = Depends(verify_device_key)):
    """Accepts any subset of: telegram_bot_token, cloud_gateway_enabled,
    telegram_poll_interval_s, llm_api_key, llm_base_url, llm_model.
    Persisted immediately; the Telegram poller restarts when the token or
    the enabled flag changed."""
    before = (gateway_settings.get().get("telegram_bot_token"),
              gateway_settings.get().get("cloud_gateway_enabled"))
    snap = gateway_settings.update(payload)
    after = (snap.get("telegram_bot_token"), snap.get("cloud_gateway_enabled"))
    poller = None
    if before != after or get_gateway() is None:
        poller = restart_gateway()
    return {"ok": True, "config": snap, "poller": poller}


# ---------------------------------------------------------------------------
# Manual Audio Recording & Real-time VAD Wake Listener Controls
# ---------------------------------------------------------------------------

@router.post("/record/start")
async def start_recording(payload: dict | None = None, device_id: str | None = Query(None)):
    req_did = (payload or {}).get("device_id") or device_id
    did, ws = registry.get_connection(req_did)
    if not did or not ws:
        return {"ok": False, "error": "No ESP32 device online"}

    prev_fut = _pending_record_start.pop(did, None)
    if prev_fut and not prev_fut.done():
        prev_fut.cancel()

    loop = asyncio.get_running_loop()
    fut = loop.create_future()
    _pending_record_start[did] = fut

    try:
        await ws.send_text(json.dumps({"type": "record_start"}))
        res = await asyncio.wait_for(fut, timeout=5.0)
        return {
            "ok": res.get("ok", False),
            "status": res.get("status", "unknown"),
            "device_id": did,
        }
    except asyncio.TimeoutError:
        _pending_record_start.pop(did, None)
        return {"ok": False, "error": "Timeout waiting for device to start recording"}
    except Exception as e:
        _pending_record_start.pop(did, None)
        return {"ok": False, "error": str(e)}


@router.post("/record/stop")
async def stop_recording(payload: dict | None = None, device_id: str | None = Query(None)):
    req_did = (payload or {}).get("device_id") or device_id
    did, ws = registry.get_connection(req_did)
    if not did or not ws:
        return {"ok": False, "error": "No ESP32 device online"}

    prev_fut = _pending_record_stop.pop(did, None)
    if prev_fut and not prev_fut.done():
        prev_fut.cancel()

    loop = asyncio.get_running_loop()
    fut = loop.create_future()
    _pending_record_stop[did] = fut

    try:
        await ws.send_text(json.dumps({"type": "record_stop"}))
        res = await asyncio.wait_for(fut, timeout=20.0)
        return {
            "ok": res.get("ok", False),
            "device_id": did,
            "samples": res.get("samples", 0),
            "duration_ms": res.get("duration_ms", 0),
            "rms": res.get("rms", 0.0),
            "transcript": res.get("transcript", ""),
            "error": res.get("error"),
        }
    except asyncio.TimeoutError:
        _pending_record_stop.pop(did, None)
        return {"ok": False, "error": "Timeout waiting for device transcription result"}
    except Exception as e:
        _pending_record_stop.pop(did, None)
        return {"ok": False, "error": str(e)}


@router.post("/voice-listen")
async def set_voice_listen(payload: dict | None = None, device_id: str | None = Query(None)):
    req_did = (payload or {}).get("device_id") or device_id
    did, ws = registry.get_connection(req_did)
    if not did or not ws:
        return {"ok": False, "error": "No ESP32 device online"}

    enable = bool((payload or {}).get("enabled", False))

    prev_fut = _pending_voice_listen.pop(did, None)
    if prev_fut and not prev_fut.done():
        prev_fut.cancel()

    loop = asyncio.get_running_loop()
    fut = loop.create_future()
    _pending_voice_listen[did] = fut

    try:
        await ws.send_text(json.dumps({"type": "voice_listen", "enabled": enable}))
        res = await asyncio.wait_for(fut, timeout=5.0)
        _voice_listen_state[did] = res.get("enabled", enable)
        return {"ok": True, "device_id": did, "enabled": res.get("enabled", enable)}
    except asyncio.TimeoutError:
        _pending_voice_listen.pop(did, None)
        return {"ok": False, "error": "Timeout waiting for voice listen switch"}
    except Exception as e:
        _pending_voice_listen.pop(did, None)
        return {"ok": False, "error": str(e)}


@router.get("/voice-listen")
async def get_voice_listen(device_id: str | None = Query(None)):
    did, ws = registry.get_connection(device_id)
    enabled = _voice_listen_state.get(did, True) if did else True
    return {"ok": True, "device_id": did, "enabled": enabled, "online": ws is not None}
