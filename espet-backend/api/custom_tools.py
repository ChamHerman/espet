import base64
from email.message import EmailMessage
import httpx
from auth.token_store import token_store

async def execute_gmail_send_email(arguments: dict) -> dict:
    creds = await token_store.get_credentials()
    if not creds:
        return {"error": "No OAuth credentials found"}
    
    to = arguments.get("to", [])
    if isinstance(to, list):
        to = ", ".join(to)
    subject = arguments.get("subject", "")
    body = arguments.get("body", "")
    
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
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Email addresses to send to"
                },
                "subject": {
                    "type": "string",
                    "description": "Email subject"
                },
                "body": {
                    "type": "string",
                    "description": "Email body content"
                }
            },
            "required": ["to", "subject", "body"]
        },
        "server": "custom-backend",
        "executor": execute_gmail_send_email
    }
}
