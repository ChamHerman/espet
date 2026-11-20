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


