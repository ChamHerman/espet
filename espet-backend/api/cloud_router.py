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
    Sends sync_now frame to connected device WebSocket.
    Returns True if sent, False if offline.
    """
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
    await websocket.send_text(json.dumps({"type": "hello_ack",
                                          "device_id": device_id,
                                          "chat_id": primary_chat}))
    logger.info("Device %s connected (fw=%s) — %d device(s) online",
                device_id, hello.get("fw", "?"), len(registry.connected_devices()))

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
                gw = get_gateway()
                if gw is None:
                    await websocket.send_text(json.dumps(
                        {"type": "error", "detail": "gateway not configured"}))
                    continue
                chat_id = str(frame.get("chat_id", ""))
                text = str(frame.get("text", ""))
                if not chat_id or not text:
                    await websocket.send_text(json.dumps(
                        {"type": "error", "detail": "message_out needs chat_id+text"}))
                    continue
                # Fire-and-forget so the receive loop keeps reading frames
                asyncio.create_task(_send_logged(gw, chat_id, text))

            elif ftype == "photo_out":
                gw = get_gateway()
                if gw is None:
                    continue
                import base64
                chat_id = str(frame.get("chat_id", ""))
                caption = str(frame.get("caption", ""))
                try:
                    image = base64.b64decode(frame.get("image_b64", ""))
                except Exception:
                    await websocket.send_text(json.dumps(
                        {"type": "error", "detail": "photo_out image_b64 invalid"}))
                    continue
                asyncio.create_task(_photo_logged(gw, chat_id, image, caption))

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
                try:
                    rag_res = await asyncio.to_thread(
                        rag_service.search,
                        query=user_query,
                        device_id=device_id,
                        top_k=settings.rag_top_k,
                        threshold=settings.rag_threshold,
                    )
                    if rag_res.get("results"):
                        lines = ["## Retrieved Long-Term Memory (via SQLite-Vec RAG):"]
                        for item in rag_res["results"]:
                            lines.append(f"- [{item['source_type']}]: {item['content']}")
                        injected_block = "\n".join(lines)

                        if messages and isinstance(messages[0], dict) and messages[0].get("role") == "system":
                            orig_content = messages[0].get("content") or ""
                            messages[0]["content"] = f"{orig_content}\n\n{injected_block}".strip()
                        else:
                            messages.insert(0, {"role": "system", "content": injected_block})

                        logger.info(
                            "rag: Injected %d memory chunk(s) for device %s (%.2f ms)",
                            len(rag_res["results"]),
                            device_id,
                            rag_res.get("latency_ms", 0.0),
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


NOISE_TOOLS = {
    "get_current_time",
    "cron_list",
    "gpio_read",
    "gpio_read_all",
    "read_file",
    "list_dir",
    "memory_search",
    "memory_save",
}

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
            if name and name not in NOISE_TOOLS:
                summary = f"Action: executed tool {name} with args {args[:120]}"
                await asyncio.to_thread(
                    rag_service.add_memory,
                    device_id=device_id,
                    content=summary,
                    source_type="action",
                    source_ref="relay_tool",
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
