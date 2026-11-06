"""
Host E2E tests for ESPET hybrid mode (docs/HYBRID_MODE_PLAN.md §4).

No hardware needed:
  - a mock Telegram HTTP server (records sendMessage / sendPhoto calls)
  - the cloud_router app under a real uvicorn server, with a real Telegram
    Gateway poller running inside it
  - a real WebSocket device client (websockets) playing the ESP32 role

Run:  python tests/test_cloud_gateway.py        (from espet-backend/)
  or: python -m pytest tests/test_cloud_gateway.py -v

Covers: TC-01..TC-08  (TC-09 hardware E2E is deferred to build/flash).
"""

from __future__ import annotations

import asyncio
import base64
import json
import socket
import sys
import tempfile
import threading
import time
from pathlib import Path

import httpx
import uvicorn
from fastapi import FastAPI, Request
from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from api.cloud_router import router as cloud_router, registry
from services.device_registry import DeviceRegistry
from services.gateway_settings import gateway_settings
from services.telegram_gateway import get_gateway
from config import settings

DEVICE_KEY = "test-device-key"
# Self-contained: override whatever the developer's local .env contains
settings.device_api_key = DEVICE_KEY
settings.cloud_gateway_enabled = True

# ---------------------------------------------------------------------------
# Mock Telegram server — records calls, hands out scripted updates
# ---------------------------------------------------------------------------

mock_records: dict[str, list] = {"sendMessage": [], "sendPhoto": [], "chatCompletions": []}
mock_queue: list = []                     # updates waiting to be fetched
mock_lock = threading.Lock()

mock_app = FastAPI()


@mock_app.post("/v1/chat/completions")
async def mock_chat_completions(request: Request):
    auth = request.headers.get("authorization", "")
    body = await request.json()
    with mock_lock:
        mock_records["chatCompletions"].append({"auth": auth, "body": body})
    return {
        "id": "chatcmpl-test",
        "object": "chat.completion",
        "created": int(time.time()),
        "model": body.get("model", "gpt-4o-mini"),
        "choices": [
            {
                "index": 0,
                "message": {
                    "role": "assistant",
                    "content": "Hello from mock cloud LLM!",
                },
                "finish_reason": "stop",
            }
        ],
    }


@mock_app.get("/bot{token}/getUpdates")
def mock_get_updates(token: str, offset: int = 0, timeout: int = 0,
                     limit: int = 10):
    with mock_lock:
        out = list(mock_queue)
        mock_queue.clear()
    return {"ok": True, "result": out}


@mock_app.post("/bot{token}/sendMessage")
async def mock_send_message(token: str, request: Request):
    body = await request.json()
    mock_records["sendMessage"].append(body)
    return {"ok": True, "result": {"message_id": 999}}


@mock_app.post("/bot{token}/sendPhoto")
async def mock_send_photo(token: str, request: Request):
    form = await request.form()
    photo = form.get("photo")
    content = await photo.read() if photo is not None else b""
    mock_records["sendPhoto"].append({
        "chat_id": form.get("chat_id"),
        "caption": form.get("caption"),
        "photo_len": len(content),
        "photo_head": content[:4],
    })
    return {"ok": True, "result": {"message_id": 998}}


def _free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def _serve_in_thread(app: FastAPI, port: int) -> threading.Thread:
    config = uvicorn.Config(app, host="127.0.0.1", port=port,
                            log_level="error", lifespan="on")
    thread = threading.Thread(target=uvicorn.Server(config).run, daemon=True)
    thread.start()
    deadline = time.time() + 5
    while time.time() < deadline:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.3).close()
            return thread
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not come up in 5s")


# ---------------------------------------------------------------------------
# Gateway app (production wiring in miniature: registry + gateway + router)
# ---------------------------------------------------------------------------

tmp_dir = tempfile.mkdtemp(prefix="espet-gw-test-")
mock_port = _free_port()
_serve_in_thread(mock_app, mock_port)
MOCK_BASE = f"http://127.0.0.1:{mock_port}"

gw_app = FastAPI()
gw_app.include_router(cloud_router)


@gw_app.on_event("startup")
async def _start_gateway():
    from services.telegram_gateway import TelegramGateway, set_gateway
    gw = TelegramGateway(
        registry,
        bot_token="TESTTOKEN",
        api_base=MOCK_BASE,
        poll_interval_s=0.3,
        long_poll_s=0,
        offset_path=str(Path(tmp_dir) / "telegram_offset.txt"),
    )
    from services.telegram_gateway import set_gateway as _sg
    _sg(gw)
    gw.start()


@gw_app.on_event("shutdown")
async def _stop_gateway():
    gw = get_gateway()
    if gw:
        await gw.stop()


gw_port = _free_port()
_serve_in_thread(gw_app, gw_port)
GW_BASE = f"ws://127.0.0.1:{gw_port}/api/cloud/ws/device"

http = httpx.Client(base_url=f"http://127.0.0.1:{gw_port}",
                    headers={"Authorization": f"Bearer {DEVICE_KEY}"},
                    timeout=5)


def _reset_state():
    """Fresh registry + mock state per test. Bindings and settings persist to tmp files."""
    registry.connections.clear()
    registry.chat_to_device.clear()
    registry.persist_path = Path(tmp_dir) / "cloud_bindings.json"
    gateway_settings.persist_path = Path(tmp_dir) / "gateway_config.json"
    (Path(tmp_dir) / "gateway_config.json").write_text("{}")
    gateway_settings._load()
    mock_records["sendMessage"].clear()
    mock_records["sendPhoto"].clear()
    with mock_lock:
        mock_records["chatCompletions"].clear()
        mock_queue.clear()


async def _wait_for(predicate, timeout=5.0, step=0.05):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if predicate():
            return True
        await asyncio.sleep(step)
    return False


async def _connect_device(device_id: str):
    """Open a device WS and complete the hello handshake. Returns the ws."""
    ws = await connect(f"{GW_BASE}?key={DEVICE_KEY}")
    await ws.send(json.dumps({"type": "hello", "device_id": device_id,
                              "fw": "test"}))
    reply = json.loads(await asyncio.wait_for(ws.recv(), 5))
    assert reply["type"] == "hello_ack", reply
    return ws


# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------

async def _tc01_auth_reject():
    # no key at all -> server accepts then closes with 4401
    ws = await connect(GW_BASE)
    try:
        await ws.recv()
        raise AssertionError("expected close, got a frame")
    except ConnectionClosed as e:
        assert e.rcvd is not None and e.rcvd.code == 4401, \
            f"expected 4401, got {e.rcvd}"
    # wrong key
    ws = await connect(GW_BASE + "?key=WRONG")
    try:
        await ws.recv()
        raise AssertionError("expected close, got a frame")
    except ConnectionClosed as e:
        assert e.rcvd is not None and e.rcvd.code == 4401


async def _tc02_hello_handshake():
    async with connect(f"{GW_BASE}?key={DEVICE_KEY}") as ws:
        await ws.send(json.dumps({"type": "hello", "device_id": "ESP-TC02",
                                  "fw": "test"}))
        reply = json.loads(await asyncio.wait_for(ws.recv(), 5))
        assert reply["type"] == "hello_ack", reply
        assert reply["device_id"] == "ESP-TC02"


async def _tc03_inbound_routing():
    _reset_state()
    async with connect(f"{GW_BASE}?key={DEVICE_KEY}") as ws:
        await ws.send(json.dumps({"type": "hello", "device_id": "ESP-TC03"}))
        await ws.recv()  # hello_ack

        # queued AFTER connect so the poller routes it to this device
        with mock_lock:
            mock_queue.append({
                "update_id": 101,
                "message": {"message_id": 815, "text": "hi",
                            "chat": {"id": -100, "title": "ESPET FYP"},
                            "from": {"first_name": "J"}}})
        frame = json.loads(await asyncio.wait_for(ws.recv(), 5))
        assert frame["type"] == "message_in", frame
        assert frame["chat_id"] == "-100"
        assert frame["text"] == "hi"
        assert frame["message_id"] == 815
        assert frame["from_name"] == "J"
        assert registry.device_for_chat("-100") == "ESP-TC03"


async def _tc04_outbound_send():
    _reset_state()
    async with connect(f"{GW_BASE}?key={DEVICE_KEY}") as ws:
        await ws.send(json.dumps({"type": "hello", "device_id": "ESP-TC04"}))
        await ws.recv()  # hello_ack
        await ws.send(json.dumps({"type": "message_out",
                                  "chat_id": "-100", "text": "yo"}))
        ok = await _wait_for(
            lambda: any(m.get("chat_id") == "-100" and m.get("text") == "yo"
                        for m in mock_records["sendMessage"]))
        assert ok, f"sendMessage not recorded: {mock_records['sendMessage']}"
        assert len(mock_records["sendMessage"]) == 1


async def _tc05_auto_bind():
    _reset_state()
    async with connect(f"{GW_BASE}?key={DEVICE_KEY}") as ws:
        await ws.send(json.dumps({"type": "hello", "device_id": "ESP-TC05"}))
        await ws.recv()  # hello_ack

        with mock_lock:
            mock_queue.append({
                "update_id": 201,
                "message": {"message_id": 900, "text": "bind me",
                            "chat": {"id": -200}, "from": {"first_name": "X"}}})
        frame = json.loads(await asyncio.wait_for(ws.recv(), 5))
        assert frame["type"] == "message_in" and frame["chat_id"] == "-200"
        bindings = http.get("/api/cloud/bindings").json()["bindings"]
        assert bindings.get("-200") == "ESP-TC05", bindings


async def _tc06_binding_persistence():
    # self-contained: write a binding, then load it in a fresh instance
    path = Path(tmp_dir) / "bindings_persist_test.json"
    if path.exists():
        path.unlink()
    r1 = DeviceRegistry(persist_path=path)
    r1.bind("-200", "ESP-TC05")
    r2 = DeviceRegistry(persist_path=path)
    assert r2.device_for_chat("-200") == "ESP-TC05", r2.bindings()


async def _tc07_disconnect_safety():
    _reset_state()
    ws = await connect(f"{GW_BASE}?key={DEVICE_KEY}")
    await ws.send(json.dumps({"type": "hello", "device_id": "ESP-TC07"}))
    await ws.recv()  # hello_ack
    await ws.close()
    assert await _wait_for(
        lambda: "ESP-TC07" not in registry.connected_devices()), \
        "connection not cleaned up after close"

    # an update for a chat with no online device must be dropped gracefully
    with mock_lock:
        mock_queue.append({
            "update_id": 301,
            "message": {"message_id": 901, "text": "nobody home",
                        "chat": {"id": -300}, "from": {"first_name": "Y"}}})
    await asyncio.sleep(1.0)
    gw = get_gateway()
    assert gw is not None and gw._task and not gw._task.done(), \
        "gateway poller died after handling an undeliverable message"

    # reconnect: device registers again and receives the next update
    ws2 = await _connect_device("ESP-TC07")
    try:
        with mock_lock:
            mock_queue.append({
                "update_id": 302,
                "message": {"message_id": 902, "text": "back again",
                            "chat": {"id": -300}, "from": {"first_name": "Y"}}})
        frame = json.loads(await asyncio.wait_for(ws2.recv(), 5))
        assert frame["type"] == "message_in" and frame["text"] == "back again"
    finally:
        await ws2.close()


async def _tc08_photo_out():
    _reset_state()
    fake_jpeg = b"\xff\xd8\xff\xe0" + b"FAKEJPEGDATA" * 4
    async with connect(f"{GW_BASE}?key={DEVICE_KEY}") as ws:
        await ws.send(json.dumps({"type": "hello", "device_id": "ESP-TC08"}))
        await ws.recv()  # hello_ack
        await ws.send(json.dumps({
            "type": "photo_out", "chat_id": "-100",
            "image_b64": base64.b64encode(fake_jpeg).decode(),
            "caption": "snap",
        }))
        ok = await _wait_for(lambda: bool(mock_records["sendPhoto"]))
        assert ok, "sendPhoto not recorded"
        rec = mock_records["sendPhoto"][0]
        assert rec["chat_id"] == "-100"
        assert rec["caption"] == "snap"
        assert rec["photo_head"] == b"\xff\xd8\xff\xe0"
        assert rec["photo_len"] == len(fake_jpeg)


async def _tc09_gateway_config():
    _reset_state()
    # Read current gateway configuration
    resp = http.get("/api/cloud/gateway-config")
    assert resp.status_code == 200, f"get gateway-config failed: {resp.text}"
    data = resp.json()
    assert "telegram_bot_token" in data
    assert "cloud_gateway_enabled" in data
    assert "llm_base_url" in data

    # Update runtime gateway configuration (poll interval + model override)
    put_resp = http.put(
        "/api/cloud/gateway-config",
        json={
            "telegram_poll_interval_s": 1.5,
            "llm_model": "mock-test-model-v1",
        },
    )
    assert put_resp.status_code == 200, f"put gateway-config failed: {put_resp.text}"
    put_data = put_resp.json()
    assert put_data.get("ok") is True
    assert put_data["config"]["telegram_poll_interval_s"] == 1.5
    assert put_data["config"]["llm_model"] == "mock-test-model-v1"

    # Confirm snapshot reflects persisted values
    fresh = http.get("/api/cloud/gateway-config").json()
    assert fresh["telegram_poll_interval_s"] == 1.5
    assert fresh["llm_model"] == "mock-test-model-v1"


async def _tc10_llm_relay():
    _reset_state()
    # Configure backend gateway settings to point to the mock LLM server
    gateway_settings.update({
        "llm_base_url": f"{MOCK_BASE}/v1",
        "llm_api_key": "sk-mock-key-12345",
        "llm_model": "mock-relay-model",
    })

    async with connect(f"{GW_BASE}?key={DEVICE_KEY}") as ws:
        await ws.send(json.dumps({"type": "hello", "device_id": "ESP-TC10"}))
        await ws.recv()  # hello_ack

        # Send llm.request frame as the device agent loop would
        req_id = "req-10-abc"
        req_body = json.dumps({
            "model": "client-requested-model",
            "messages": [{"role": "user", "content": "Ping mock LLM"}],
        })
        await ws.send(json.dumps({
            "type": "llm.request",
            "request_id": req_id,
            "body": req_body,
        }))

        # Receive llm.response frame
        raw = await asyncio.wait_for(ws.recv(), 5.0)
        resp_frame = json.loads(raw)
        assert resp_frame.get("type") == "llm.response", f"Unexpected frame: {resp_frame}"
        assert resp_frame.get("request_id") == req_id
        assert resp_frame.get("ok") is True
        assert resp_frame.get("status") == 200

        # Verify body inside response contains the mock LLM reply
        resp_body = json.loads(resp_frame.get("body", "{}"))
        assert "choices" in resp_body
        assert resp_body["choices"][0]["message"]["content"] == "Hello from mock cloud LLM!"

        # Verify mock LLM server received the injected server key and model override
        assert len(mock_records["chatCompletions"]) == 1
        recorded = mock_records["chatCompletions"][0]
        assert recorded["auth"] == "Bearer sk-mock-key-12345"
        assert recorded["body"]["model"] == "mock-relay-model"
        assert recorded["body"]["messages"][0]["content"] == "Ping mock LLM"


# ---------------------------------------------------------------------------
# pytest-compatible wrappers + standalone runner
# ---------------------------------------------------------------------------

CASES = [
    ("TC-01 auth_reject", _tc01_auth_reject),
    ("TC-02 hello_handshake", _tc02_hello_handshake),
    ("TC-03 inbound_routing", _tc03_inbound_routing),
    ("TC-04 outbound_send", _tc04_outbound_send),
    ("TC-05 auto_bind", _tc05_auto_bind),
    ("TC-06 binding_persistence", _tc06_binding_persistence),
    ("TC-07 disconnect_safety", _tc07_disconnect_safety),
    ("TC-08 photo_out", _tc08_photo_out),
    ("TC-09 gateway_config", _tc09_gateway_config),
    ("TC-10 llm_relay", _tc10_llm_relay),
]


def test_tc01_auth_reject():
    asyncio.run(_tc01_auth_reject())


def test_tc02_hello_handshake():
    asyncio.run(_tc02_hello_handshake())


def test_tc03_inbound_routing():
    asyncio.run(_tc03_inbound_routing())


def test_tc04_outbound_send():
    asyncio.run(_tc04_outbound_send())


def test_tc05_auto_bind():
    asyncio.run(_tc05_auto_bind())


def test_tc06_binding_persistence():
    asyncio.run(_tc06_binding_persistence())


def test_tc07_disconnect_safety():
    asyncio.run(_tc07_disconnect_safety())


def test_tc08_photo_out():
    asyncio.run(_tc08_photo_out())


def test_tc09_gateway_config():
    asyncio.run(_tc09_gateway_config())


def test_tc10_llm_relay():
    asyncio.run(_tc10_llm_relay())


if __name__ == "__main__":
    failures = 0
    print(f"Gateway: {GW_BASE}\nMock Telegram: {MOCK_BASE}\n" + "-" * 50)
    for name, fn in CASES:
        try:
            asyncio.run(fn())
            print(f"  PASS  {name}")
        except Exception as e:
            failures += 1
            print(f"  FAIL  {name}: {type(e).__name__}: {e}")
    print("-" * 50)
    total = len(CASES)
    print(f"{total - failures}/{total} test cases passed")
    raise SystemExit(1 if failures else 0)
