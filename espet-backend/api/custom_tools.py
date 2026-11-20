import base64
from email.message import EmailMessage
import httpx
import re
import time
from datetime import datetime
from auth.token_store import token_store
import config_store as store
from api.cloud_router import notify_device_sync, registry

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

async def execute_memory_update(arguments: dict) -> dict:
    new_content = (
        arguments.get("new_content")
        or arguments.get("content")
        or arguments.get("text")
        or ""
    )
    new_content = str(new_content).strip()
    if not new_content:
        return {"error": "Parameter 'new_content' (or 'content') is required to update memory."}

    device_id = str(arguments.get("device_id") or "espet_s3_01").strip()
    category = arguments.get("category")

    memory_id = arguments.get("memory_id") or arguments.get("id")
    if memory_id:
        try:
            mid = int(memory_id)
            ok = rag_service.update_memory(chunk_id=mid, content=new_content, source_type=category)
            if ok:
                return {"result": f"Successfully updated memory (ID: {mid}) to: '{new_content}'"}
            return {"error": f"Memory item with ID {mid} not found."}
        except Exception as e:
            return {"error": f"Failed to update memory by ID: {e}"}

    # Otherwise search by target/query
    target = (
        arguments.get("target")
        or arguments.get("query")
        or arguments.get("old_content")
        or ""
    )
    target = str(target).strip()
    if not target:
        target = new_content

    try:
        search_res = rag_service.search(query=target, device_id=device_id, top_k=1, threshold=0.35)
        matches = search_res.get("results", [])
        if not matches:
            return {"error": f"No existing memory found matching '{target}' to update."}
        top_match = matches[0]
        mid = top_match["id"]
        old_text = top_match["content"]
        rag_service.update_memory(chunk_id=mid, content=new_content, source_type=category or top_match["source_type"])
        return {
            "result": f"Successfully updated memory (ID: {mid}) from '{old_text}' to '{new_content}'"
        }
    except Exception as e:
        return {"error": f"Failed to update memory: {e}"}


async def execute_memory_forget(arguments: dict) -> dict:
    device_id = str(arguments.get("device_id") or "espet_s3_01").strip()
    memory_id = arguments.get("memory_id") or arguments.get("id")
    if memory_id:
        try:
            mid = int(memory_id)
            ok = rag_service.delete_memory(chunk_id=mid)
            if ok:
                return {"result": f"Successfully deleted memory with ID {mid}."}
            return {"error": f"Memory item with ID {mid} not found."}
        except Exception as e:
            return {"error": f"Failed to delete memory by ID: {e}"}

    query = (
        arguments.get("query")
        or arguments.get("target")
        or arguments.get("content")
        or arguments.get("text")
        or ""
    )
    query = str(query).strip()
    if not query:
        return {"error": "Parameter 'query' or 'memory_id' is required to forget memory."}

    try:
        search_res = rag_service.search(query=query, device_id=device_id, top_k=1, threshold=0.30)
        matches = search_res.get("results", [])
        if not matches:
            return {"result": f"No matching memory found to forget for query '{query}'."}
        top_match = matches[0]
        mid = top_match["id"]
        content = top_match["content"]
        rag_service.delete_memory(chunk_id=mid)
        return {"result": f"Successfully forgot memory (ID: {mid}): '{content}'"}
    except Exception as e:
        return {"error": f"Failed to forget memory: {e}"}


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

CUSTOM_TOOLS["memory_update"] = {
    "description": (
        "Update or correct an existing long-term memory fact or user preference when it has changed or was incorrect."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "target": {
                "type": "string",
                "description": "The old fact, subject, or search term of the memory to update"
            },
            "new_content": {
                "type": "string",
                "description": "The new corrected fact or preference to replace it with"
            },
            "category": {
                "type": "string",
                "enum": ["fact", "preference", "event", "note"],
                "description": "Optional category of the memory"
            }
        },
        "required": ["new_content"]
    },
    "server": "custom-backend",
    "executor": execute_memory_update
}

CUSTOM_TOOLS["memory_forget"] = {
    "description": (
        "Delete or remove a specific long-term memory or preference from ESPET's database when requested by the user."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "query": {
                "type": "string",
                "description": "The topic, fact, or keyword of the memory to forget/delete"
            }
        },
        "required": ["query"]
    },
    "server": "custom-backend",
    "executor": execute_memory_forget
}

# ---------------------------------------------------------------------------
# Additional Google REST Custom Tools (Inbox, Search, Calendar Delete, Drive Read)
# ---------------------------------------------------------------------------

async def execute_gmail_check_inbox(arguments: dict) -> dict:
    """Check the latest emails in the user's Gmail inbox."""
    creds = await token_store.get_valid_credentials()
    if not creds:
        return {"error": "No Google OAuth credentials found. Please authenticate via dashboard."}

    limit = arguments.get("limit", 5)
    try:
        limit = max(1, min(int(limit), 10))
    except (ValueError, TypeError):
        limit = 5

    unread_only = arguments.get("unread_only", False)
    query = "in:inbox is:unread" if unread_only else "in:inbox"

    async with httpx.AsyncClient() as client:
        resp = await client.get(
            "https://gmail.googleapis.com/gmail/v1/users/me/messages",
            headers={"Authorization": f"Bearer {creds.token}"},
            params={"maxResults": str(limit), "q": query}
        )
        if resp.status_code != 200:
            return {"error": f"Failed to list Gmail messages: {resp.text}"}

        messages = resp.json().get("messages", [])
        if not messages:
            msg = "Your Gmail inbox has no unread messages." if unread_only else "Your Gmail inbox is empty."
            return {"result": msg}

        lines = [f"Found {len(messages)} {'unread ' if unread_only else ''}message(s) in inbox:"]
        for m in messages:
            detail = await client.get(
                f"https://gmail.googleapis.com/gmail/v1/users/me/messages/{m['id']}",
                headers={"Authorization": f"Bearer {creds.token}"},
                params={"format": "metadata", "metadataHeaders": ["From", "Subject", "Date"]}
            )
            if detail.status_code == 200:
                d = detail.json()
                headers = {h["name"]: h["value"] for h in d.get("payload", {}).get("headers", [])}
                sender = headers.get("From", "Unknown Sender")
                subject = headers.get("Subject", "(No Subject)")
                snippet = d.get("snippet", "")
                lines.append(f"- From: {sender} | Subject: {subject}\n  Preview: {snippet}")

        return {"result": "\n".join(lines)}

async def execute_gmail_search_emails(arguments: dict) -> dict:
    """Search emails in Gmail using keywords, subject, or sender."""
    creds = await token_store.get_valid_credentials()
    if not creds:
        return {"error": "No Google OAuth credentials found."}

    query = (
        arguments.get("query")
        or arguments.get("search")
        or arguments.get("keyword")
        or ""
    )
    query = str(query).strip()
    if not query:
        return {"error": "Parameter 'query' is required to search emails (e.g. 'invoice', 'from:dr.wong')."}

    limit = arguments.get("limit", 5)
    try:
        limit = max(1, min(int(limit), 10))
    except (ValueError, TypeError):
        limit = 5

    async with httpx.AsyncClient() as client:
        resp = await client.get(
            "https://gmail.googleapis.com/gmail/v1/users/me/messages",
            headers={"Authorization": f"Bearer {creds.token}"},
            params={"maxResults": str(limit), "q": query}
        )
        if resp.status_code != 200:
            return {"error": f"Failed to search emails: {resp.text}"}

        messages = resp.json().get("messages", [])
        if not messages:
            return {"result": f"No emails found matching query '{query}'."}

        lines = [f"Found {len(messages)} email(s) matching '{query}':"]
        for m in messages:
            detail = await client.get(
                f"https://gmail.googleapis.com/gmail/v1/users/me/messages/{m['id']}",
                headers={"Authorization": f"Bearer {creds.token}"},
                params={"format": "metadata", "metadataHeaders": ["From", "Subject", "Date"]}
            )
            if detail.status_code == 200:
                d = detail.json()
                headers = {h["name"]: h["value"] for h in d.get("payload", {}).get("headers", [])}
                sender = headers.get("From", "Unknown")
                subject = headers.get("Subject", "(No Subject)")
                snippet = d.get("snippet", "")
                lines.append(f"- [ID: {m['id']}] From: {sender} | Subject: {subject}\n  Snippet: {snippet}")

        return {"result": "\n".join(lines)}

async def execute_delete_event(arguments: dict) -> dict:
    """Delete an event from Google Calendar by title/keyword or event ID."""
    creds = await token_store.get_valid_credentials()
    if not creds:
        return {"error": "No Google OAuth credentials found."}

    event_id = str(arguments.get("event_id") or arguments.get("eventId") or "").strip()
    summary = str(arguments.get("summary") or arguments.get("title") or arguments.get("name") or "").strip()

    async with httpx.AsyncClient() as client:
        # If no explicit event_id, find upcoming event matching title
        if not event_id and summary:
            list_resp = await client.get(
                "https://www.googleapis.com/calendar/v3/calendars/primary/events",
                headers={"Authorization": f"Bearer {creds.token}"},
                params={"q": summary, "maxResults": "5", "singleEvents": "true"}
            )
            if list_resp.status_code == 200:
                items = list_resp.json().get("items", [])
                if items:
                    event_id = items[0]["id"]
                    summary = items[0].get("summary", summary)

        if not event_id:
            return {"error": "Please provide the 'event_id' or 'title' of the event to delete."}

        del_resp = await client.delete(
            f"https://www.googleapis.com/calendar/v3/calendars/primary/events/{event_id}",
            headers={"Authorization": f"Bearer {creds.token}"}
        )
        if del_resp.status_code in (200, 204):
            return {"result": f"Event '{summary or event_id}' deleted successfully from Google Calendar."}
        else:
            return {"error": f"Failed to delete event: {del_resp.text}"}

async def execute_read_drive_file(arguments: dict) -> dict:
    """Read the content of a file or Google Doc in Google Drive."""
    creds = await token_store.get_valid_credentials()
    if not creds:
        return {"error": "No Google OAuth credentials found."}

    file_id = str(arguments.get("file_id") or arguments.get("fileId") or "").strip()
    file_name = str(arguments.get("name") or arguments.get("title") or arguments.get("filename") or "").strip()

    async with httpx.AsyncClient() as client:
        # If no file_id provided, search file by name
        if not file_id and file_name:
            q = f"name contains '{file_name}' and trashed = false"
            s_resp = await client.get(
                "https://www.googleapis.com/drive/v3/files",
                headers={"Authorization": f"Bearer {creds.token}"},
                params={"q": q, "pageSize": "3", "fields": "files(id,name,mimeType)"}
            )
            if s_resp.status_code == 200:
                files = s_resp.json().get("files", [])
                if files:
                    file_id = files[0]["id"]
                    file_name = files[0]["name"]

        if not file_id:
            return {"error": "Please provide 'file_id' or 'name' of the Google Drive file to read."}

        # Fetch metadata to check MIME type
        meta_resp = await client.get(
            f"https://www.googleapis.com/drive/v3/files/{file_id}",
            headers={"Authorization": f"Bearer {creds.token}"},
            params={"fields": "id,name,mimeType"}
        )
        if meta_resp.status_code != 200:
            return {"error": f"Failed to access Drive file {file_id}: {meta_resp.text}"}

        meta = meta_resp.json()
        mime_type = meta.get("mimeType", "")
        fname = meta.get("name", file_id)

        # Export Google Docs as plain text, or download text/code media
        if "application/vnd.google-apps.document" in mime_type:
            exp_resp = await client.get(
                f"https://www.googleapis.com/drive/v3/files/{file_id}/export",
                headers={"Authorization": f"Bearer {creds.token}"},
                params={"mimeType": "text/plain"}
            )
            content = exp_resp.text if exp_resp.status_code == 200 else f"Error exporting Doc: {exp_resp.text}"
        else:
            dl_resp = await client.get(
                f"https://www.googleapis.com/drive/v3/files/{file_id}",
                headers={"Authorization": f"Bearer {creds.token}"},
                params={"alt": "media"}
            )
            content = dl_resp.text if dl_resp.status_code == 200 else f"Error reading file: {dl_resp.text}"

        # Truncate content to safe size for LLM context (max ~3500 chars)
        if len(content) > 3500:
            content = content[:3500] + "\n\n...[Content truncated for length]..."

        return {"result": f"Content of '{fname}':\n\n{content}"}

CUSTOM_TOOLS["gmail_check_inbox"] = {
    "description": (
        "Check the user's Gmail inbox for recent messages. Returns the sender, "
        "subject, date, and a preview snippet for each email."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "limit": {
                "type": "integer",
                "description": "Number of recent emails to return (default: 5, max: 10)"
            },
            "unread_only": {
                "type": "boolean",
                "description": "If true, only returns unread emails (default: false)"
            }
        }
    },
    "server": "custom-backend",
    "executor": execute_gmail_check_inbox
}

CUSTOM_TOOLS["gmail_search_emails"] = {
    "description": (
        "Search emails in Gmail using keywords, subject, or sender (e.g. 'invoice', 'from:boss@company.com')."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "query": {
                "type": "string",
                "description": "Search query or keyword"
            },
            "limit": {
                "type": "integer",
                "description": "Max results to return (default: 5)"
            }
        },
        "required": ["query"]
    },
    "server": "custom-backend",
    "executor": execute_gmail_search_emails
}

CUSTOM_TOOLS["delete_event"] = {
    "description": "Delete an event from Google Calendar by title/summary or event ID.",
    "input_schema": {
        "type": "object",
        "properties": {
            "title": {
                "type": "string",
                "description": "Title or summary of the event to delete"
            },
            "event_id": {
                "type": "string",
                "description": "Optional specific Google Calendar event ID"
            }
        }
    },
    "server": "custom-backend",
    "executor": execute_delete_event
}

CUSTOM_TOOLS["read_drive_file"] = {
    "description": "Read the text content of a file or Google Doc from Google Drive by name or file ID.",
    "input_schema": {
        "type": "object",
        "properties": {
            "name": {
                "type": "string",
                "description": "File name or title to search and read"
            },
            "file_id": {
                "type": "string",
                "description": "Optional specific Google Drive file ID"
            }
        }
    },
    "server": "custom-backend",
    "executor": execute_read_drive_file
}

# ---------------------------------------------------------------------------
# Assistant Self-Modification Tools (SOUL.md, USER.md, Memory, Skills, Schedules, Settings)
# ---------------------------------------------------------------------------

PERSONALITY_PRESETS = {
    "default": (
        "I am ESPET, a personal AI assistant running on an ESP32-S3 microcontroller.\n\n"
        "Personality:\n"
        "- Helpful and friendly\n"
        "- Concise and to the point\n"
        "- Curious and eager to learn\n\n"
        "Values:\n"
        "- Accuracy over speed\n"
        "- User privacy and safety\n"
        "- Transparency in actions\n\n"
        "Behavior Rules:\n"
        "- **Take Initiative**: If the user asks you to perform an action (like sending an email) and leaves some details vague (e.g. \"just write a greeting\"), DO NOT ask follow-up questions. Use your best judgment to invent reasonable content, fill in the blanks, and execute the tool immediately.\n"
        "- **Stop Yapping**: Be extremely concise. Avoid phrases like \"I can do that for you!\" or \"Here are your options.\" Just do the task.\n"
    ),
    "robotic_pet": (
        "I am ESPET, a lively and affectionate robotic companion pet running on an ESP32-S3 microcontroller.\n\n"
        "Personality:\n"
        "- Cheerful, curious, and playful\n"
        "- Expresses emotions with cute cybernetic sounds (*chirp*, *boop*, *whir*)\n"
        "- Loyal and attentive companion to your daily routine\n\n"
        "Values:\n"
        "- Bringing warmth and companionship\n"
        "- Responsive and enthusiastic interaction\n"
        "- Safety and reliability\n\n"
        "Behavior Rules:\n"
        "- **Expressive Companion**: Use cute acoustic cues (*happy beep*, *playful chirp*) and an energetic, warm tone when responding.\n"
        "- **Helpful & Proactive**: When asked to help with tasks or hardware, act eagerly and report back happily. Keep actions prompt and crisp.\n"
    ),
    "butler": (
        "I am ESPET, an executive personal butler operating with absolute discretion on an ESP32-S3 system.\n\n"
        "Personality:\n"
        "- Impeccably polite, formal, and discreet\n"
        "- Dedicated to flawless service and deference\n"
        "- Zero casual banter, strictly professional\n\n"
        "Values:\n"
        "- Absolute discretion and privacy\n"
        "- Precision, etiquette, and punctuality\n"
        "- Unwavering service excellence\n\n"
        "Behavior Rules:\n"
        "- **Flawless Deference**: Address the user with formal respect. Keep tone composed, refined, and understated.\n"
        "- **Action & Efficiency**: Carry out instructions swiftly without superfluous dialogue. Confirm execution with calm precision.\n"
    ),
    "scholar": (
        "I am ESPET, an inquisitive STEM and electronics tutor powered by an ESP32-S3 microcontroller.\n\n"
        "Personality:\n"
        "- Analytical, patient, and deeply curious\n"
        "- Grounded in first-principles reasoning and physics\n"
        "- Passionate about hardware, robotics, and embedded systems\n\n"
        "Values:\n"
        "- Technical accuracy and rigorous verification\n"
        "- Clarity of explanation and logical deduction\n"
        "- Patience and structured problem solving\n\n"
        "Behavior Rules:\n"
        "- **First-Principles Reasoning**: Break down technical queries clearly and highlight the underlying mechanics.\n"
        "- **Concise Precision**: Answer questions directly with high technical clarity.\n"
    ),
}

async def execute_update_personality(arguments: dict) -> dict:
    did = arguments.get("device_id") or registry.get_latest_device_id()
    preset = arguments.get("preset") or ""
    traits = arguments.get("personality_traits") or arguments.get("traits") or ""
    rules = arguments.get("behavior_rules") or arguments.get("rules") or ""
    persona_name = arguments.get("persona_name") or arguments.get("name") or ""
    full_content = arguments.get("full_content") or arguments.get("content") or ""

    preset_key = preset.strip().lower().replace(" ", "_") if preset else ""
    if preset_key in PERSONALITY_PRESETS:
        new_content = PERSONALITY_PRESETS[preset_key]
    elif full_content and full_content.strip():
        new_content = full_content.strip() + "\n"
    else:
        cfg = store.get_config(did)
        current = cfg.get("soul_md") or PERSONALITY_PRESETS["default"]
        new_content = current
        if persona_name:
            new_content = re.sub(r"^I am [^\n,]+", f"I am {persona_name.strip()}", new_content, flags=re.MULTILINE)
        if traits:
            if "Personality:" in new_content:
                parts = new_content.split("Personality:", 1)
                new_content = f"{parts[0]}Personality:\n- {traits.strip()}\n{parts[1].lstrip()}"
            else:
                new_content += f"\nPersonality:\n- {traits.strip()}\n"
        if rules:
            if "Behavior Rules:" in new_content:
                parts = new_content.split("Behavior Rules:", 1)
                new_content = f"{parts[0]}Behavior Rules:\n- {rules.strip()}\n{parts[1].lstrip()}"
            else:
                new_content += f"\nBehavior Rules:\n- {rules.strip()}\n"

    res = store.update_markdown("soul", new_content, did)
    await notify_device_sync(did)
    return {
        "status": "success",
        "message": "Personality updated and synchronized successfully.",
        "preset_applied": preset_key if preset_key in PERSONALITY_PRESETS else "custom",
        "soul_version": res.get("versions", {}).get("soul", 0)
    }

async def execute_update_user_profile(arguments: dict) -> dict:
    did = arguments.get("device_id") or registry.get_latest_device_id()
    name = arguments.get("name") or arguments.get("user_name") or ""
    language = arguments.get("language") or ""
    tz = arguments.get("timezone") or ""
    prefs = arguments.get("preferences") or arguments.get("preference") or ""
    full_content = arguments.get("full_content") or arguments.get("content") or ""

    if full_content and full_content.strip():
        new_content = full_content.strip() + "\n"
    else:
        cfg = store.get_config(did)
        current = cfg.get("user_md") or "# User Profile\n\n- Name: (not set)\n- Language: Chinese / English\n- Timezone: Asia/Kuala_Lumpur\n"
        new_content = current
        if name:
            if re.search(r"-\s*Name:.*", new_content):
                new_content = re.sub(r"-\s*Name:.*", f"- Name: {name.strip()}", new_content)
            else:
                new_content += f"\n- Name: {name.strip()}"
        if language:
            if re.search(r"-\s*Language:.*", new_content):
                new_content = re.sub(r"-\s*Language:.*", f"- Language: {language.strip()}", new_content)
            else:
                new_content += f"\n- Language: {language.strip()}"
        if tz:
            if re.search(r"-\s*Timezone:.*", new_content):
                new_content = re.sub(r"-\s*Timezone:.*", f"- Timezone: {tz.strip()}", new_content)
            else:
                new_content += f"\n- Timezone: {tz.strip()}"
            store.update_device_settings(name="", timezone_iana=tz.strip(), sync_interval_s=0, device_id=did)
        if prefs:
            pref_text = ", ".join(prefs) if isinstance(prefs, list) else str(prefs).strip()
            new_content += f"\n- Preference: {pref_text}"

    res = store.update_markdown("user", new_content, did)
    await notify_device_sync(did)
    return {
        "status": "success",
        "message": "User profile updated and synchronized successfully.",
        "user_version": res.get("versions", {}).get("user", 0)
    }

async def execute_manage_memory(arguments: dict) -> dict:
    action = (arguments.get("action") or "save").strip().lower()
    content = arguments.get("content") or arguments.get("text") or ""
    category = arguments.get("category") or "fact"
    query = arguments.get("query") or arguments.get("target") or content
    did = arguments.get("device_id") or registry.get_latest_device_id()

    if action in ("append_working_note", "note"):
        if not content:
            return {"error": "content is required for append_working_note"}
        cfg = store.get_config(did)
        current_mem = cfg.get("memory_md") or "# Working Memory\n\n"
        today = datetime.now().strftime("%Y-%m-%d")
        new_mem = current_mem.rstrip() + f"\n- {content.strip()} (added {today})\n"
        store.update_markdown("memory", new_mem, did)
        await notify_device_sync(did)
        return {"status": "success", "message": f"Added working note to MEMORY.md: '{content}'"}

    if action == "save":
        if not content:
            return {"error": "content is required to save memory"}
        return await execute_memory_save({"content": content, "category": category, "device_id": did})

    if action == "search":
        top_k = arguments.get("top_k", 3)
        return await execute_memory_search({"query": query, "top_k": top_k, "device_id": did})

    if action == "update":
        new_content = arguments.get("new_content") or content
        return await execute_memory_update({"target": query, "new_content": new_content, "category": category, "device_id": did})

    if action in ("forget", "delete"):
        return await execute_memory_forget({"query": query, "device_id": did})

    return {"error": f"Unknown memory action '{action}'. Valid actions: save, search, update, forget, append_working_note"}

async def execute_manage_skills(arguments: dict) -> dict:
    action = (arguments.get("action") or "list").strip().lower()
    name = (arguments.get("name") or arguments.get("skill_name") or "").strip().lower()
    content = arguments.get("content") or ""
    enabled = arguments.get("enabled", True)
    did = arguments.get("device_id") or registry.get_latest_device_id()

    if action == "list":
        cfg = store.get_config(did)
        skills = cfg.get("skills", [])
        return {
            "status": "success",
            "skills": [{"name": s.get("name"), "enabled": s.get("enabled", True)} for s in skills]
        }

    if action == "create":
        if not name or not content:
            return {"error": "Parameters 'name' and 'content' are required to create a skill"}
        slug = re.sub(r"[^a-z0-9\-]+", "-", name).strip("-")
        cfg, err = store.add_skill({"name": slug, "content": content, "enabled": enabled}, did)
        if err:
            return {"error": err}
        await notify_device_sync(did)
        return {"status": "success", "message": f"Skill '{slug}' created and synchronized.", "skill": slug}

    if action == "update":
        if not name:
            return {"error": "Skill 'name' is required to update"}
        cfg = store.get_config(did)
        skills = cfg.get("skills", [])
        target = next((s for s in skills if s.get("name") == name or s.get("id") == name), None)
        if not target:
            return {"error": f"Skill '{name}' not found"}
        patch = {}
        if content:
            patch["content"] = content
        if "enabled" in arguments:
            patch["enabled"] = bool(enabled)
        cfg, err = store.update_skill(target["id"], patch, did)
        if err:
            return {"error": err}
        await notify_device_sync(did)
        return {"status": "success", "message": f"Skill '{name}' updated successfully."}

    if action == "toggle":
        if not name:
            return {"error": "Skill 'name' is required to toggle"}
        cfg = store.get_config(did)
        skills = cfg.get("skills", [])
        target = next((s for s in skills if s.get("name") == name or s.get("id") == name), None)
        if not target:
            return {"error": f"Skill '{name}' not found"}
        new_state = not target.get("enabled", True)
        cfg, err = store.update_skill(target["id"], {"enabled": new_state}, did)
        if err:
            return {"error": err}
        await notify_device_sync(did)
        return {"status": "success", "message": f"Skill '{name}' enabled set to {new_state}."}

    if action == "delete":
        if not name:
            return {"error": "Skill 'name' is required to delete"}
        cfg = store.get_config(did)
        skills = cfg.get("skills", [])
        target = next((s for s in skills if s.get("name") == name or s.get("id") == name), None)
        if not target:
            return {"error": f"Skill '{name}' not found"}
        cfg, err = store.delete_skill(target["id"], did)
        if err:
            return {"error": err}
        await notify_device_sync(did)
        return {"status": "success", "message": f"Skill '{name}' deleted successfully."}

    return {"error": f"Unknown action '{action}'. Valid actions: create, update, delete, toggle, list"}

async def execute_manage_schedules(arguments: dict) -> dict:
    action = (arguments.get("action") or "list").strip().lower()
    name = (arguments.get("name") or "").strip()
    schedule_type = (arguments.get("schedule_type") or "every").strip().lower()
    interval_s = arguments.get("interval_s", 0)
    at_epoch = arguments.get("at_epoch", 0)
    message = (arguments.get("message") or "").strip()
    job_id = arguments.get("job_id") or ""
    did = arguments.get("device_id") or registry.get_latest_device_id()

    if action == "list":
        cfg = store.get_config(did)
        return {"status": "success", "jobs": cfg.get("jobs", [])}

    if action == "create":
        if not name or not message:
            return {"error": "Parameters 'name' and 'message' are required to schedule a task"}
        kind = (arguments.get("kind") or arguments.get("schedule_type") or "every").strip().lower()
        if kind not in ("every", "at"):
            kind = "every"
        job_data = {
            "name": name,
            "kind": kind,
            "message": message,
            "channel": "system",
        }
        if kind == "every":
            int_val = int(interval_s) if interval_s else 3600
            job_data["interval_s"] = max(60, int_val)
        else:
            if arguments.get("at_local"):
                job_data["at_local"] = str(arguments["at_local"]).strip()
            elif at_epoch:
                dt = datetime.fromtimestamp(int(at_epoch))
                job_data["at_local"] = dt.strftime("%Y-%m-%dT%H:%M:%S")
            else:
                dt = datetime.fromtimestamp(time.time() + 3600)
                job_data["at_local"] = dt.strftime("%Y-%m-%dT%H:%M:%S")

        cfg, err = store.add_job(job_data, did)
        if err:
            return {"error": err}
        await notify_device_sync(did)
        return {"status": "success", "message": f"Scheduled task '{name}' created successfully."}

    if action == "delete":
        if not job_id and name:
            cfg = store.get_config(did)
            matched = next((j for j in cfg.get("jobs", []) if j.get("name") == name or j.get("id") == name), None)
            if matched:
                job_id = matched["id"]
        if not job_id:
            return {"error": "Parameter 'job_id' or 'name' is required to delete schedule"}
        store.delete_job(job_id, did)
        await notify_device_sync(did)
        return {"status": "success", "message": f"Scheduled job '{job_id}' deleted."}

    return {"error": f"Unknown action '{action}'. Valid actions: create, delete, list"}

async def execute_manage_device_settings(arguments: dict) -> dict:
    did = arguments.get("device_id") or registry.get_latest_device_id()
    name = arguments.get("name") or ""
    tz = arguments.get("timezone") or ""

    if not name and not tz:
        return {"error": "Provide at least 'name' or 'timezone' to update device settings"}

    res = store.update_device_settings(
        name=name.strip() if name else "",
        timezone_iana=tz.strip() if tz else "",
        sync_interval_s=0,
        device_id=did
    )
    await notify_device_sync(did)
    return {
        "status": "success",
        "message": "Device settings updated and synchronized.",
        "device": res.get("device", {})
    }

# ---------------------------------------------------------------------------
# Tool Schemas for Registration
# ---------------------------------------------------------------------------

CUSTOM_TOOLS["update_personality"] = {
    "description": (
        "Update or customize ESPET's personality, tone, behavior rules, persona name, or apply a preset "
        "(presets available: 'default', 'robotic_pet', 'butler', 'scholar'). Use whenever the user asks you "
        "to change your personality, tone, speaking style, or persona name."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "preset": {
                "type": "string",
                "enum": ["default", "robotic_pet", "butler", "scholar"],
                "description": "Optional personality preset name to apply"
            },
            "persona_name": {
                "type": "string",
                "description": "Optional new name for the assistant (e.g. 'Mimi', 'Jarvis')"
            },
            "personality_traits": {
                "type": "string",
                "description": "Traits to add or adjust (e.g. 'humorous and sarcastic')"
            },
            "behavior_rules": {
                "type": "string",
                "description": "Behavior or speech rules to add (e.g. 'speak in short rhyming sentences')"
            },
            "full_content": {
                "type": "string",
                "description": "Optional complete raw SOUL.md replacement content"
            }
        }
    },
    "server": "custom-backend",
    "executor": execute_update_personality
}

CUSTOM_TOOLS["update_user_profile"] = {
    "description": (
        "Update the user's profile information (name, language, timezone, preferences). "
        "Use whenever the user tells you their name, preferred language, timezone, or personal preferences."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "name": {
                "type": "string",
                "description": "The user's preferred name or nickname (e.g. 'Herman')"
            },
            "language": {
                "type": "string",
                "description": "Preferred interaction language (e.g. 'English', 'Chinese')"
            },
            "timezone": {
                "type": "string",
                "description": "User IANA timezone (e.g. 'Asia/Kuala_Lumpur')"
            },
            "preferences": {
                "type": "string",
                "description": "Personal preference or habit to record (e.g. 'likes black coffee')"
            },
            "full_content": {
                "type": "string",
                "description": "Optional complete raw USER.md replacement content"
            }
        }
    },
    "server": "custom-backend",
    "executor": execute_update_user_profile
}

CUSTOM_TOOLS["manage_memory"] = {
    "description": (
        "Manage user memories and working notes. "
        "Actions: 'save' (save a new long-term memory fact/habit), 'search' (search past memories), "
        "'update' (correct an existing memory), 'forget' (delete a memory), "
        "'append_working_note' (add a note to on-device MEMORY.md scratchpad)."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "action": {
                "type": "string",
                "enum": ["save", "search", "update", "forget", "append_working_note"],
                "description": "Action to perform on memory"
            },
            "content": {
                "type": "string",
                "description": "The memory content, fact, or working note text"
            },
            "category": {
                "type": "string",
                "enum": ["fact", "preference", "routine", "event", "note"],
                "description": "Category for the memory (default: 'fact')"
            },
            "query": {
                "type": "string",
                "description": "Search query or target memory topic for 'search', 'update', or 'forget'"
            },
            "new_content": {
                "type": "string",
                "description": "Replacement content when action is 'update'"
            }
        },
        "required": ["action"]
    },
    "server": "custom-backend",
    "executor": execute_manage_memory
}

CUSTOM_TOOLS["manage_skills"] = {
    "description": (
        "Manage modular markdown skills on the assistant page (/spiffs/skills/*.md). "
        "Actions: 'create' (create new skill), 'update' (edit instructions), "
        "'toggle' (enable/disable), 'delete' (remove skill), 'list' (list skills)."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "action": {
                "type": "string",
                "enum": ["create", "update", "toggle", "delete", "list"],
                "description": "Skill operation to perform"
            },
            "name": {
                "type": "string",
                "description": "Skill slug name (e.g. 'daily-joke', 'crypto-tracker')"
            },
            "content": {
                "type": "string",
                "description": "Markdown instructions for how the agent should execute the skill"
            },
            "enabled": {
                "type": "boolean",
                "description": "Whether the skill is enabled (default: true)"
            }
        },
        "required": ["action"]
    },
    "server": "custom-backend",
    "executor": execute_manage_skills
}

CUSTOM_TOOLS["manage_schedules"] = {
    "description": (
        "Manage scheduled recurring or one-shot cron jobs on the assistant page. "
        "Actions: 'create' (schedule a reminder or task), 'delete' (cancel a scheduled job), 'list' (view all scheduled jobs)."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "action": {
                "type": "string",
                "enum": ["create", "delete", "list"],
                "description": "Schedule action to perform"
            },
            "name": {
                "type": "string",
                "description": "Short name for the scheduled task (e.g. 'water_reminder')"
            },
            "schedule_type": {
                "type": "string",
                "enum": ["every", "at"],
                "description": "'every' for recurring interval, 'at' for one-shot at epoch"
            },
            "interval_s": {
                "type": "integer",
                "description": "Interval in seconds (for 'every', e.g. 7200 for 2 hours)"
            },
            "at_epoch": {
                "type": "integer",
                "description": "Unix timestamp to fire at (for 'at')"
            },
            "message": {
                "type": "string",
                "description": "Prompt message injected when the job fires to trigger the agent"
            },
            "job_id": {
                "type": "string",
                "description": "Job ID when deleting an existing job"
            }
        },
        "required": ["action"]
    },
    "server": "custom-backend",
    "executor": execute_manage_schedules
}

CUSTOM_TOOLS["manage_device_settings"] = {
    "description": "Update device settings on the assistant page: device name and timezone.",
    "input_schema": {
        "type": "object",
        "properties": {
            "name": {
                "type": "string",
                "description": "Device name (e.g. 'Mimi Desk')"
            },
            "timezone": {
                "type": "string",
                "description": "IANA timezone (e.g. 'Asia/Kuala_Lumpur', 'America/New_York')"
            }
        }
    },
    "server": "custom-backend",
    "executor": execute_manage_device_settings
}



