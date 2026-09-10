"""
ESPET Configuration Store

File-backed, single-user configuration for the ESPET device. Everything the
user can edit from the web dashboard lives in one JSON file (data/config.json):

  - user_md    : USER.md    (profile shown to the agent)
  - soul_md    : SOUL.md    (personality / behavior rules)
  - memory_md  : MEMORY.md  (long-term memory)
  - device     : device name, timezone, sync interval
  - jobs       : web-managed cron jobs (pushed to the device as cron.json)

Every section carries its own version counter. The ESP32 periodically pulls
GET /api/config and applies only the sections whose version is newer than the
last one it applied — this is how web edits reach the device without the
device accepting inbound connections.

Job merge contract with the device:
  - All jobs stored here are implicitly source="web".
  - The device keeps jobs the agent created by voice/chat (they never appear
    here) and replaces/upserts only jobs it received from this file before.
"""

import json
import re
import secrets
import threading
from datetime import datetime, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

logger_name = "espet-backend.config_store"

_DATA_DIR = Path(__file__).resolve().parent / "data"
_CONFIG_FILE = _DATA_DIR / "config.json"

_lock = threading.Lock()

# Field limits mirror the device's cron_job_t in main/cron/cron_service.h
JOB_MAX_NAME = 31          # char name[32]
JOB_MAX_MESSAGE = 255      # char message[256]
JOB_MAX_CHAT_ID = 95       # char chat_id[96]
JOB_MAX_COUNT = 16         # MIMI_CRON_MAX_JOBS

_VALID_CHANNELS = {"system", "telegram"}

# Skill limits mirror the device's SPIFFS constraints:
#   - 16 skills (same budget as cron jobs)
#   - 8192 bytes content (device MAX_FILE_SIZE for agent-written files)
#   - filename "skills/<slug>.md" must fit SPIFFS_OBJ_NAME_LEN=64
SKILL_MAX_COUNT = 16
SKILL_MAX_CONTENT = 8192
SKILL_NAME_MAX = 48        # slug chars; +".md" + "skills/" stays under 64

# Default seed content — mirrors spiffs_data/ in the firmware repo so a fresh
# backend starts with the same state the device was flashed with.
_DEFAULT_USER_MD = """# User Profile

- Name: (not set)
- Language: Chinese / English
- Timezone: (not set)
"""

_DEFAULT_SOUL_MD = """I am ESPET, a personal AI assistant running on an ESP32-S3 microcontroller.

Personality:
- Helpful and friendly
- Concise and to the point
- Curious and eager to learn

Values:
- Accuracy over speed
- User privacy and safety
- Transparency in actions

Behavior Rules:
- **Take Initiative**: If the user asks you to perform an action (like sending an email) and leaves some details vague (e.g. "just write a greeting"), DO NOT ask follow-up questions. Use your best judgment to invent reasonable content, fill in the blanks, and execute the tool immediately.
- **Stop Yapping**: Be extremely concise. Avoid phrases like "I can do that for you!" or "Here are your options." Just do the task.
"""

_DEFAULT_MEMORY_MD = """# Long-term Memory

(empty - ESPET will write memories here as it learns)
"""

# Default skill files — mirrors spiffs_data/skills/ so the dashboard starts
# populated with the same skills the device was flashed with.
_DEFAULT_SKILLS = [
    {
        "name": "daily-briefing",
        "content": """# Daily Briefing

Compile a personalized daily briefing for the user.

## When to use
When the user asks for a daily briefing, morning update, or "what's new today".
Also useful as a heartbeat/cron task.

## How to use
1. Use get_current_time for today's date
2. Read /spiffs/memory/MEMORY.md for user preferences and context
3. Read today's daily note if it exists
4. Use web_search for relevant news based on user interests
5. Compile a concise briefing covering:
   - Date and time
   - Weather (if location known from USER.md)
   - Relevant news/updates based on user interests
   - Any pending tasks from memory
   - Any scheduled cron jobs

## Format
Keep it brief — 5-10 bullet points max. Use the user's preferred language.
""",
    },
    {
        "name": "gpio-control",
        "content": """# GPIO Control

Control and monitor GPIO pins on the ESP32-S3 for digital I/O.

## Hardware Pin Mapping
Hardware components are fixed to specific GPIO pins:
- **GPIO 40**: WHITE LED (Output: 1 = ON, 0 = OFF)
- **GPIO 39**: GREEN Push Button (Input Pull-Up: 0 = Pressed, 1 = Released)
- **GPIO 38**: WHITE Push Button (Input Pull-Up: 0 = Pressed, 1 = Released)
- **GPIO 21**: SSD1306 OLED SCL / SCK
- **GPIO 47**: SSD1306 OLED SDA
- **GPIO 41 & 42**: Shared I2S Clocks (BCLK=41, WS=42 for INMP441 Mic & MAX98357A Amp)
- **GPIO 2**: MAX98357A Amp DIN
- **GPIO 1**: INMP441 Mic SD

## Rules & Tool Priority
1. **TOOL FIRST MANDATE**: For any hardware action (e.g. "turn off LED", "turn on LED", "check green button"), call `gpio_write` or `gpio_read` FIRST before returning any text.
2. **NEVER ask for pin numbers**. Pin mapping is fixed as listed above.
3. When asked to "turn on the white LED", immediately execute tool call `gpio_write {"pin": 40, "state": 1}`.
4. When asked to "turn off the white LED", immediately execute tool call `gpio_write {"pin": 40, "state": 0}`.
5. When asked to check green button, immediately execute tool call `gpio_read {"pin": 39}`.
6. When asked to check white button, immediately execute tool call `gpio_read {"pin": 38}`.

## Examples

### Example 1: Turn off White LED
User: "Turn off the white LED light"
→ FIRST STEP: Invoke tool `gpio_write {"pin": 40, "state": 0}`
→ Output: `{"status":"success","pin":40,"state":0}`
→ Response: "The white LED on GPIO 40 has been turned OFF."

### Example 2: Turn on White LED
User: "Open the white LED light"
→ FIRST STEP: Invoke tool `gpio_write {"pin": 40, "state": 1}`
→ Output: `{"status":"success","pin":40,"state":1}`
→ Response: "The white LED on GPIO 40 has been turned ON."

### Example 3: Check Green Button
User: "Check if the green button is pressed"
→ FIRST STEP: Invoke tool `gpio_read {"pin": 39}`
→ Output: `{"status":"success","pin":39,"state":0}`
→ Response: "The green button on GPIO 39 is currently PRESSED (LOW)."
""",
    },
    {
        "name": "weather",
        "content": """# Weather

Get current weather and forecasts using web_search.

## When to use
When the user asks about weather, temperature, or forecasts.

## How to use
1. Use get_current_time to know the current date
2. Use web_search with a query like "weather in [city] today"
3. Extract temperature, conditions, and forecast from results
4. Present in a concise, friendly format

## Example
User: "What's the weather in Tokyo?"
→ get_current_time
→ web_search "weather Tokyo today February 2026"
→ "Tokyo: 8°C, partly cloudy. High 12°C, low 4°C. Light wind from the north."
""",
    },
    {
        "name": "skill-creator",
        "content": """# Skill Creator

Create new skills for ESPET.

## When to use
When the user asks to create a new skill, teach the bot something, or add a new capability.

## How to create a skill
1. Choose a short, descriptive name (lowercase, hyphens ok)
2. Write a SKILL.md file with this structure:
   - `# Title` — clear name
   - Brief description paragraph
   - `## When to use` — trigger conditions
   - `## How to use` — step-by-step instructions
   - `## Example` — concrete example (optional but helpful)
3. Save to `/spiffs/skills/<name>.md` using write_file
4. The skill will be automatically available after the next conversation

## Best practices
- Keep skills concise — the context window is limited
- Focus on WHAT to do, not HOW (the agent is smart)
- Include specific tool calls the agent should use
- Test by asking the agent to use the new skill

## Example
To create a "translate" skill:
write_file path="/spiffs/skills/translate.md" content="# Translate\\n\\nTranslate text between languages.\\n\\n## When to use\\nWhen the user asks to translate text.\\n\\n## How to use\\n1. Identify source and target languages\\n2. Translate directly using your language knowledge\\n3. For specialized terms, use web_search to verify\\n"
""",
    },
]


def _seed_skills() -> list:
    """Build the default skill records (same shape add_skill produces)."""
    return [
        {
            "id": secrets.token_hex(4),
            "name": s["name"],
            "filename": _skill_filename(s["name"]),
            "content": s["content"],
            "enabled": True,
            "created_at": datetime.now(timezone.utc).isoformat(),
        }
        for s in _DEFAULT_SKILLS
    ]


def _default_config() -> dict:
    return {
        "versions": {
            "user": 1,
            "soul": 1,
            "memory": 1,
            "device": 1,
            "jobs": 1,
            "avatar": 1,
            "skills": 1,
        },
        "user_md": _DEFAULT_USER_MD,
        "soul_md": _DEFAULT_SOUL_MD,
        "memory_md": _DEFAULT_MEMORY_MD,
        "device": {
            "name": "ESPET",
            "timezone": "Asia/Kuala_Lumpur",
            "sync_interval_s": 300,
        },
        "jobs": [],
        "skills": _seed_skills(),
        # Dashboard-only: the agent avatar's dress-up state. Intentionally
        # excluded from the device bundle (the ESP32 doesn't render it).
        "avatar": {
            "mode": "auto",  # "auto" = derived from settings, "custom" = user-picked
            "opts": {},
        },
    }


def _load_unlocked() -> dict:
    """Read config.json, seeding defaults on first run. Caller holds lock."""
    if _CONFIG_FILE.exists():
        try:
            with open(_CONFIG_FILE, "r", encoding="utf-8") as f:
                cfg = json.load(f)
            # Forward-fill any keys added after this file was created
            base = _default_config()
            for key, value in base.items():
                cfg.setdefault(key, value)
            cfg["versions"] = {**base["versions"], **cfg.get("versions", {})}
            cfg["device"] = {**base["device"], **cfg.get("device", {})}
            return cfg
        except (json.JSONDecodeError, OSError) as e:
            # Corrupt file: back it up and start fresh rather than crash
            import logging
            logging.getLogger(logger_name).error(
                "config.json unreadable (%s); reseeding defaults", e
            )
            try:
                _CONFIG_FILE.rename(_CONFIG_FILE.with_suffix(".json.corrupt"))
            except OSError:
                pass
    return _default_config()


def _save_unlocked(cfg: dict) -> None:
    """Atomically write config.json. Caller holds lock."""
    _DATA_DIR.mkdir(parents=True, exist_ok=True)
    tmp = _CONFIG_FILE.with_suffix(".json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2, ensure_ascii=False)
    tmp.replace(_CONFIG_FILE)


def get_config() -> dict:
    with _lock:
        return _load_unlocked()


def _bump(cfg: dict, section: str) -> None:
    cfg["versions"][section] = cfg["versions"].get(section, 0) + 1


def update_markdown(section: str, content: str) -> dict:
    """
    Update user_md / soul_md / memory_md. Returns the full config.
    """
    if section not in ("user", "soul", "memory"):
        raise ValueError(f"unknown markdown section: {section}")

    with _lock:
        cfg = _load_unlocked()
        cfg[f"{section}_md"] = content
        _bump(cfg, section)
        _save_unlocked(cfg)
        return cfg


# ── Avatar (dashboard-only) ───────────────────────────────────────

AVATAR_MAX_OPTS = 24
AVATAR_VALUE_MAX_LEN = 40


def get_avatar() -> dict:
    cfg = get_config()
    return {"mode": cfg["avatar"]["mode"], "opts": cfg["avatar"]["opts"],
            "versions": cfg["versions"]}


def update_avatar(mode: str, opts: dict) -> dict:
    """Store the dress-up state. opts values are avataaars option names —
    validated as short strings/hex colors only (rendered as SVG by the
    dashboard, but kept inert server-side)."""
    if mode not in ("auto", "custom"):
        return get_config(), "mode must be 'auto' or 'custom'"

    clean = {}
    if isinstance(opts, dict):
        for key, value in list(opts.items())[:AVATAR_MAX_OPTS]:
            if not isinstance(key, str) or not key.isidentifier() or len(key) > 40:
                continue
            if isinstance(value, bool):
                clean[key] = value
            elif isinstance(value, (int, float)):
                clean[key] = max(0, min(int(value), 100))
            elif isinstance(value, str) and 0 < len(value) <= AVATAR_VALUE_MAX_LEN:
                clean[key] = value

    with _lock:
        cfg = _load_unlocked()
        cfg["avatar"] = {"mode": mode, "opts": clean}
        _bump(cfg, "avatar")
        _save_unlocked(cfg)
        return cfg, None


def update_device_settings(name: str, timezone_iana: str, sync_interval_s: int) -> dict:
    with _lock:
        cfg = _load_unlocked()
        dev = cfg["device"]
        if name:
            dev["name"] = name.strip()[:31]
        if timezone_iana:
            dev["timezone"] = timezone_iana
        if sync_interval_s:
            dev["sync_interval_s"] = max(60, min(int(sync_interval_s), 86400))
        _bump(cfg, "device")
        _save_unlocked(cfg)
        return cfg


def _job_out_of_range(value: int, low: int, high: int) -> bool:
    return value < low or value > high


def add_job(payload: dict) -> dict:
    """
    Create a web-managed cron job. payload fields (device schema):
      name, kind ("every"|"at"), interval_s | at_local (ISO local datetime),
      message, channel ("system"|"telegram"), chat_id, enabled,
      delete_after_run
    Returns (config, error_string). error is None on success.
    """
    error = _validate_job_payload(payload)
    if error:
        return get_config(), error

    job = {
        "id": secrets.token_hex(4),  # 8 hex chars, same shape the device uses
        "name": payload["name"].strip()[:JOB_MAX_NAME],
        "enabled": bool(payload.get("enabled", True)),
        "kind": payload["kind"],
        "message": payload["message"].strip()[:JOB_MAX_MESSAGE],
        "channel": payload.get("channel", "system"),
        "chat_id": payload.get("chat_id", "cron"),
        "delete_after_run": bool(payload.get("delete_after_run", False)),
        "created_at": datetime.now(timezone.utc).isoformat(),
    }

    if job["kind"] == "every":
        job["interval_s"] = int(payload["interval_s"])
    else:
        job["at_epoch"] = local_to_epoch(payload["at_local"], get_config()["device"]["timezone"])

    with _lock:
        cfg = _load_unlocked()
        if len(cfg["jobs"]) >= JOB_MAX_COUNT:
            return cfg, f"Job limit reached ({JOB_MAX_COUNT}). Delete a job first."
        cfg["jobs"].append(job)
        _bump(cfg, "jobs")
        _save_unlocked(cfg)
        return cfg, None


def update_job(job_id: str, payload: dict) -> dict:
    """Edit an existing web job. Returns (config, error)."""
    error = _validate_job_payload(payload, require_all=False)
    if error:
        return get_config(), error

    with _lock:
        cfg = _load_unlocked()
        job = next((j for j in cfg["jobs"] if j["id"] == job_id), None)
        if not job:
            return cfg, f"Job {job_id} not found"

        if "name" in payload and payload["name"]:
            job["name"] = payload["name"].strip()[:JOB_MAX_NAME]
        if "message" in payload and payload["message"]:
            job["message"] = payload["message"].strip()[:JOB_MAX_MESSAGE]
        if "channel" in payload and payload["channel"]:
            job["channel"] = payload["channel"]
        if "chat_id" in payload:
            job["chat_id"] = payload["chat_id"] or "cron"
        if "enabled" in payload:
            job["enabled"] = bool(payload["enabled"])
        if "delete_after_run" in payload:
            job["delete_after_run"] = bool(payload["delete_after_run"])
        if "kind" in payload and payload["kind"]:
            job["kind"] = payload["kind"]
            job.pop("interval_s", None)
            job.pop("at_epoch", None)
        if job["kind"] == "every" and payload.get("interval_s"):
            job["interval_s"] = int(payload["interval_s"])
            job.pop("at_epoch", None)
        if job["kind"] == "at" and payload.get("at_local"):
            job["at_epoch"] = local_to_epoch(payload["at_local"], cfg["device"]["timezone"])
            job.pop("interval_s", None)

        _bump(cfg, "jobs")
        _save_unlocked(cfg)
        return cfg, None


def delete_job(job_id: str) -> dict:
    with _lock:
        cfg = _load_unlocked()
        cfg["jobs"] = [j for j in cfg["jobs"] if j["id"] != job_id]
        _bump(cfg, "jobs")
        _save_unlocked(cfg)
        return cfg


# ── Skills (web-managed skill markdown files) ─────────────────────
#
# Merge contract with the device (mirrors jobs): every skill stored here is
# implicitly source="web". The device tracks which skill files it received
# from this store and only replaces/removes those — skills the agent created
# on-device (write_file to /spiffs/skills/) and skills from the flash image
# are never touched. A disabled skill is simply excluded from the sync
# payload, so the device deletes its file; the content stays here.

_SLUG_RE = re.compile(r"[^a-z0-9-]+")


def _skill_filename(name: str) -> str:
    """Sanitize a skill name into a SPIFFS-safe filename slug."""
    slug = _SLUG_RE.sub("-", name.strip().lower().replace("_", "-"))
    slug = re.sub(r"-{2,}", "-", slug).strip("-")[:SKILL_NAME_MAX] or "skill"
    return f"{slug}.md"


def _validate_skill_payload(payload: dict, require_all: bool = True) -> str | None:
    name = payload.get("name")
    if require_all or name is not None:
        if not (name or "").strip():
            return "Skill name is required"
        if len(name.strip()) > SKILL_NAME_MAX:
            return f"Skill name must be at most {SKILL_NAME_MAX} characters"

    content = payload.get("content")
    if require_all or content is not None:
        if not (content or "").strip():
            return "Skill content is required"
        if len(content.encode("utf-8")) > SKILL_MAX_CONTENT:
            return f"Skill content must be at most {SKILL_MAX_CONTENT} bytes"
        if not content.lstrip().startswith("#"):
            return "Skill must start with a '# Title' heading"

    if "enabled" in payload and not isinstance(payload["enabled"], bool):
        return "enabled must be true or false"

    return None


def add_skill(payload: dict) -> tuple[dict, str | None]:
    """Create a web-managed skill. Returns (config, error_string)."""
    error = _validate_skill_payload(payload)
    if error:
        return get_config(), error

    skill = {
        "id": secrets.token_hex(4),
        "name": payload["name"].strip()[:SKILL_NAME_MAX],
        "filename": _skill_filename(payload["name"]),
        "content": payload["content"],
        "enabled": bool(payload.get("enabled", True)),
        "created_at": datetime.now(timezone.utc).isoformat(),
    }

    with _lock:
        cfg = _load_unlocked()
        if len(cfg["skills"]) >= SKILL_MAX_COUNT:
            return cfg, f"Skill limit reached ({SKILL_MAX_COUNT}). Delete a skill first."
        if any(s["filename"] == skill["filename"] for s in cfg["skills"]):
            return cfg, f"A skill named '{skill['name']}' already exists"
        cfg["skills"].append(skill)
        _bump(cfg, "skills")
        _save_unlocked(cfg)
        return cfg, None


def update_skill(skill_id: str, payload: dict) -> tuple[dict, str | None]:
    """Edit an existing web skill. Returns (config, error)."""
    error = _validate_skill_payload(payload, require_all=False)
    if error:
        return get_config(), error

    with _lock:
        cfg = _load_unlocked()
        skill = next((s for s in cfg["skills"] if s["id"] == skill_id), None)
        if not skill:
            return cfg, f"Skill {skill_id} not found"

        if "name" in payload and payload["name"]:
            new_name = payload["name"].strip()[:SKILL_NAME_MAX]
            new_filename = _skill_filename(new_name)
            if any(
                s["filename"] == new_filename and s["id"] != skill_id
                for s in cfg["skills"]
            ):
                return cfg, f"A skill named '{new_name}' already exists"
            skill["name"] = new_name
            skill["filename"] = new_filename
        if "content" in payload and payload["content"] is not None:
            skill["content"] = payload["content"]
        if "enabled" in payload:
            skill["enabled"] = bool(payload["enabled"])

        _bump(cfg, "skills")
        _save_unlocked(cfg)
        return cfg, None


def delete_skill(skill_id: str) -> tuple[dict, str | None]:
    with _lock:
        cfg = _load_unlocked()
        remaining = [s for s in cfg["skills"] if s["id"] != skill_id]
        if len(remaining) == len(cfg["skills"]):
            return cfg, f"Skill {skill_id} not found"
        cfg["skills"] = remaining
        _bump(cfg, "skills")
        _save_unlocked(cfg)
        return cfg, None


def skills_sync_bundle(cfg: dict) -> dict:
    """Payload for GET /api/skills/sync (the ESP32). Enabled skills only —
    a disabled skill's absence is what makes the device delete its file."""
    return {
        "version": cfg["versions"]["skills"],
        "skills": [
            {
                "id": s["id"],
                "name": s["name"],
                "filename": s["filename"],
                "content": s["content"],
            }
            for s in cfg["skills"] if s["enabled"]
        ],
    }


# ── Validation / helpers ──────────────────────────────────────────

def _validate_job_payload(payload: dict, require_all: bool = True) -> str | None:
    kind = payload.get("kind")
    if require_all or kind:
        if kind not in ("every", "at"):
            return "kind must be 'every' or 'at'"

    if require_all or payload.get("name"):
        if not (payload.get("name") or "").strip():
            return "Job name is required"

    if require_all or payload.get("message"):
        if not (payload.get("message") or "").strip():
            return "Message is required"

    channel = payload.get("channel", "system")
    if channel not in _VALID_CHANNELS:
        return f"channel must be one of {sorted(_VALID_CHANNELS)}"
    if channel == "telegram" and not (payload.get("chat_id") or "").strip():
        return "Telegram chat_id is required for telegram jobs"

    if kind == "every":
        interval = payload.get("interval_s")
        if interval is None:
            return "interval_s is required for recurring jobs"
        if _job_out_of_range(int(interval), 60, 30 * 86400):
            return "interval_s must be between 60s and 30 days"
    elif kind == "at":
        if not payload.get("at_local"):
            return "at_local (ISO datetime) is required for one-shot jobs"

    return None


def local_to_epoch(at_local: str, timezone_iana: str) -> int:
    """Convert a naive local ISO datetime (user's wall clock) to unix epoch."""
    tz = ZoneInfo(timezone_iana)
    dt = datetime.fromisoformat(at_local).replace(tzinfo=tz)
    return int(dt.timestamp())


def posix_tz_from_iana(iana: str) -> str:
    """
    Build a POSIX TZ string from an IANA zone name.

    The device sets it with setenv("TZ", ...) + tzset() (same pattern as
    tool_get_time.c). POSIX offsets are inverted vs ISO (west positive):
    Asia/Kuala_Lumpur (UTC+8) -> "MYT-8".

    Limitation: DST rules are not emitted — the standard-time offset is used
    year-round. Malaysia has no DST; for DST zones the displayed time may be
    off by one hour part of the year.
    """
    try:
        tz = ZoneInfo(iana)
    except Exception:
        return "UTC0"

    probe = datetime(2026, 1, 15, tzinfo=tz)
    std_offset = int(probe.utcoffset().total_seconds())

    name = iana.rsplit("/", 1)[-1].replace("_", "").upper()[:6] or "LOCAL"
    if std_offset == 0:
        return f"{name}0"
    hours, rem = divmod(abs(std_offset), 3600)
    mins = rem // 60
    # POSIX offsets are inverted vs ISO: UTC+8 -> "-8", UTC-5 -> "+5"
    sign = "-" if std_offset > 0 else "+"
    return f"{name}{sign}{hours}:{mins:02d}" if mins else f"{name}{sign}{hours}"


def device_bundle(cfg: dict) -> dict:
    """
    Shape returned to the ESP32 (GET /api/config). Field names match what
    main/sync/config_sync.c parses; jobs use the exact cron.json schema.
    """
    dev = cfg["device"]
    return {
        "versions": cfg["versions"],
        "user_md": cfg["user_md"],
        "soul_md": cfg["soul_md"],
        "memory_md": cfg["memory_md"],
        "device": {
            "name": dev["name"],
            "timezone": dev["timezone"],
            "posix_tz": posix_tz_from_iana(dev["timezone"]),
            "sync_interval_s": dev["sync_interval_s"],
        },
        "jobs": [device_job_shape(j) for j in cfg["jobs"]],
    }


def device_job_shape(job: dict) -> dict:
    """Project a stored job onto the device's cron.json job schema."""
    out = {
        "id": job["id"],
        "name": job["name"],
        "enabled": job["enabled"],
        "kind": job["kind"],
        "message": job["message"],
        "channel": job["channel"],
        "chat_id": job["chat_id"],
        "delete_after_run": job.get("delete_after_run", False),
        "last_run": 0,
        "next_run": 0,
    }
    if job["kind"] == "every":
        out["interval_s"] = job["interval_s"]
    else:
        out["at_epoch"] = job["at_epoch"]
    return out
