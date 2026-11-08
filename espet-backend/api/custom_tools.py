import base64
from email.message import EmailMessage
import httpx
from auth.token_store import token_store

async def execute_gmail_send_email(arguments: dict) -> dict:
    creds = await token_store.get_valid_credentials()
    if not creds:
        return {"error": "No OAuth credentials found"}
    
    to = (
        arguments.get("to")
        or arguments.get("recipient")
        or arguments.get("recipient_email")
        or arguments.get("email")
        or arguments.get("recipients")
        or ""
    )
    if isinstance(to, list):
        to = ", ".join([str(x) for x in to])
    to = str(to).strip()

    if not to:
        return {"error": "Recipient email address ('to' or 'recipient') is required"}

    subject = (
        arguments.get("subject")
        or arguments.get("title")
        or arguments.get("topic")
        or "Message from ESPET AI"
    )
    body = (
        arguments.get("body")
        or arguments.get("content")
        or arguments.get("message")
        or arguments.get("text")
        or ""
    )
    
    msg = EmailMessage()
    msg['To'] = to
    msg['Subject'] = subject
    msg.set_content(body)
    
    raw = base64.urlsafe_b64encode(msg.as_bytes()).decode('utf8')
    
    async with httpx.AsyncClient() as client:
        resp = await client.post(
            'https://gmail.googleapis.com/gmail/v1/users/me/messages/send',
            headers={'Authorization': f'Bearer {creds.token}'},
            json={'raw': raw}
        )
        if resp.status_code == 200:
            return {"result": f"Email sent successfully to {to}"}
        else:
            return {"error": f"Failed to send email: {resp.text}"}

CUSTOM_TOOLS = {
    "gmail_send_email": {
        "description": "Send an email immediately. Use this whenever the user asks to send an email.",
        "input_schema": {
            "type": "object",
            "properties": {
                "to": {
                    "type": "string",
                    "description": "Recipient email address (e.g. user@example.com)"
                },
                "recipient": {
                    "type": "string",
                    "description": "Recipient email address"
                },
                "subject": {
                    "type": "string",
                    "description": "Email subject"
                },
                "body": {
                    "type": "string",
                    "description": "Email body content"
                },
                "content": {
                    "type": "string",
                    "description": "Email body content"
                }
            }
        },
        "server": "custom-backend",
        "executor": execute_gmail_send_email
    },
    "list_recent_files": {
        "description": "List the user's recent files from Google Drive. Use this whenever the user asks to see their Drive files or recent documents.",
        "input_schema": {
            "type": "object",
            "properties": {
                "limit": {
                    "type": "integer",
                    "description": "Maximum number of files to return (default 10)"
                }
            }
        },
        "server": "custom-backend",
        "executor": None  # Assigned below after function definition
    }
}

async def execute_list_recent_files(arguments: dict) -> dict:
    creds = await token_store.get_valid_credentials()
    if not creds:
        return {"error": "No OAuth credentials found"}
    
    limit = arguments.get("limit", 10)
    try:
        limit = int(limit)
    except (ValueError, TypeError):
        limit = 10
        
    async with httpx.AsyncClient() as client:
        resp = await client.get(
            'https://www.googleapis.com/drive/v3/files',
            headers={'Authorization': f'Bearer {creds.token}'},
            params={
                'pageSize': str(limit),
                'fields': 'files(id,name,mimeType,modifiedTime)',
                'orderBy': 'modifiedTime desc'
            }
        )
        if resp.status_code == 200:
            data = resp.json()
            files = data.get("files", [])
            if not files:
                return {"result": "No recent files found in Google Drive."}
            res_lines = ["Recent Google Drive files:"]
            for f in files:
                res_lines.append(f"- {f.get('name')} (ID: {f.get('id')})")
            return {"result": "\n".join(res_lines)}
        else:
            return {"error": f"Failed to list Drive files: {resp.text}"}

CUSTOM_TOOLS["list_recent_files"]["executor"] = execute_list_recent_files

CUSTOM_TOOLS["create_event"] = {
    "description": "Create a new event on Google Calendar. Use whenever the user asks to create a calendar event.",
    "input_schema": {
        "type": "object",
        "properties": {
            "title": {
                "type": "string",
                "description": "Event title / summary (e.g. 'FYP Meeting with Dr Wong')"
            },
            "summary": {
                "type": "string",
                "description": "Event title / summary"
            },
            "start_time": {
                "type": "string",
                "description": "Start time in ISO format (e.g. '2026-08-07T14:00:00')"
            },
            "startTime": {
                "type": "string",
                "description": "Start time in ISO format (e.g. '2026-08-07T14:00:00')"
            },
            "end_time": {
                "type": "string",
                "description": "End time in ISO format (e.g. '2026-08-07T15:00:00')"
            },
            "endTime": {
                "type": "string",
                "description": "End time in ISO format (e.g. '2026-08-07T15:00:00')"
            }
        }
    },
    "server": "custom-backend",
    "executor": None
}

CUSTOM_TOOLS["list_events"] = {
    "description": "List upcoming events from Google Calendar.",
    "input_schema": {
        "type": "object",
        "properties": {
            "limit": {
                "type": "integer",
                "description": "Max events to list (default 10)"
            }
        }
    },
    "server": "custom-backend",
    "executor": None
}

async def execute_create_event(arguments: dict) -> dict:
    creds = await token_store.get_valid_credentials()
    if not creds:
        return {"error": "No OAuth credentials found"}

    summary = arguments.get("summary") or arguments.get("title") or arguments.get("event_name") or "New Event"
    start_time = arguments.get("startTime") or arguments.get("start_time") or arguments.get("start")
    end_time = arguments.get("endTime") or arguments.get("end_time") or arguments.get("end")

    if not start_time:
        return {"error": "startTime / start_time is required"}
    if not end_time:
        return {"error": "endTime / end_time is required"}

    event_body = {
        "summary": summary,
        "start": {},
        "end": {}
    }
    s_str = str(start_time)
    e_str = str(end_time)

    if 'T' in s_str or 't' in s_str:
        if not s_str.endswith("Z") and "+" not in s_str and "-" not in s_str[10:]:
            s_str += "+08:00"
        event_body["start"] = {"dateTime": s_str}
    else:
        event_body["start"] = {"date": s_str[:10]}

    if 'T' in e_str or 't' in e_str:
        if not e_str.endswith("Z") and "+" not in e_str and "-" not in e_str[10:]:
            e_str += "+08:00"
        event_body["end"] = {"dateTime": e_str}
    else:
        event_body["end"] = {"date": e_str[:10]}

    async with httpx.AsyncClient() as client:
        resp = await client.post(
            'https://www.googleapis.com/calendar/v3/calendars/primary/events',
            headers={
                'Authorization': f'Bearer {creds.token}',
                'Content-Type': 'application/json'
            },
            json=event_body
        )
        if resp.status_code in (200, 201):
            data = resp.json()
            return {"result": f"Event '{summary}' created successfully in Google Calendar for {s_str} to {e_str}!"}
        else:
            return {"error": f"Failed to create calendar event: {resp.text}"}

async def execute_list_events(arguments: dict) -> dict:
    creds = await token_store.get_valid_credentials()
    if not creds:
        return {"error": "No OAuth credentials found"}
    limit = arguments.get("limit") or arguments.get("pageSize") or 10
    try:
        limit = int(limit)
    except (ValueError, TypeError):
        limit = 10

    import datetime
    now_iso = datetime.datetime.utcnow().isoformat() + "Z"

    async with httpx.AsyncClient() as client:
        resp = await client.get(
            'https://www.googleapis.com/calendar/v3/calendars/primary/events',
            headers={'Authorization': f'Bearer {creds.token}'},
            params={
                'maxResults': str(limit),
                'orderBy': 'startTime',
                'singleEvents': 'true',
                'timeMin': now_iso
            }
        )
        if resp.status_code == 200:
            data = resp.json()
            items = data.get("items", [])
            if not items:
                return {"result": "No upcoming events found on your Google Calendar."}
            res = ["Upcoming Google Calendar events:"]
            for ev in items:
                start = ev.get("start", {}).get("dateTime") or ev.get("start", {}).get("date")
                res.append(f"- {ev.get('summary', 'No Title')} ({start})")
            return {"result": "\n".join(res)}
        else:
            return {"error": f"Failed to list events: {resp.text}"}

CUSTOM_TOOLS["create_event"]["executor"] = execute_create_event
CUSTOM_TOOLS["list_events"]["executor"] = execute_list_events

from services.rag_service import rag_service

async def execute_memory_save(arguments: dict) -> dict:
    content = (
        arguments.get("content")
        or arguments.get("text")
        or arguments.get("memory")
        or arguments.get("fact")
        or ""
    )
    content = str(content).strip()
    if not content:
        return {"error": "Parameter 'content' is required to save memory."}

    category = str(arguments.get("category") or "fact").strip().lower()
    if category not in ("fact", "preference", "event", "action", "note"):
        category = "fact"

    device_id = str(arguments.get("device_id") or "espet_s3_01").strip()
    try:
        chunk_id = rag_service.add_memory(
            device_id=device_id,
            content=content,
            source_type=category,
            source_ref="agent_tool",
        )
        return {"result": f"Successfully saved to long-term memory (ID: {chunk_id}): {content}"}
    except Exception as e:
        return {"error": f"Failed to save memory: {e}"}

async def execute_memory_search(arguments: dict) -> dict:
    query = (
        arguments.get("query")
        or arguments.get("search")
        or arguments.get("text")
        or ""
    )
    query = str(query).strip()
    if not query:
        return {"error": "Parameter 'query' is required to search memory."}

    top_k = arguments.get("top_k", 3)
    try:
        top_k = int(top_k)
    except (ValueError, TypeError):
        top_k = 3

    device_id = str(arguments.get("device_id") or "espet_s3_01").strip()
    try:
        res = rag_service.search(query=query, device_id=device_id, top_k=top_k)
        matches = res.get("results", [])
        if not matches:
            return {"result": f"No relevant long-term memories found for query '{query}'."}
        lines = [f"Found {len(matches)} relevant memory item(s):"]
        for m in matches:
            sim_pct = int(m.get("similarity", 0.0) * 100)
            lines.append(f"- [{m.get('source_type', 'fact')}] {m.get('content')} (relevance: {sim_pct}%)")
        return {"result": "\n".join(lines), "count": len(matches)}
    except Exception as e:
        return {"error": f"Failed to search memory: {e}"}

CUSTOM_TOOLS["memory_save"] = {
    "description": (
        "Save a critical fact, user preference, routine, or event into ESPET's "
        "long-term vector memory database so it persists across conversations and reboots."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "content": {
                "type": "string",
                "description": "The exact fact, habit, routine, or event to remember"
            },
            "category": {
                "type": "string",
                "enum": ["fact", "preference", "event", "action"],
                "description": "Category of the memory (default: fact)"
            }
        },
        "required": ["content"]
    },
    "server": "custom-backend",
    "executor": execute_memory_save
}

CUSTOM_TOOLS["memory_search"] = {
    "description": (
        "Search ESPET's long-term vector database for past memories, user habits, "
        "commitments, or previously learned knowledge."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "query": {
                "type": "string",
                "description": "The topic, person, or keyword to search in long-term memory"
            },
            "top_k": {
                "type": "integer",
                "description": "Maximum number of memory items to return (default: 3)"
            }
        },
        "required": ["query"]
    },
    "server": "custom-backend",
    "executor": execute_memory_search
}

