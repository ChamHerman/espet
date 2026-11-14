"""
OLED Summarizer — Intelligent Key Word Extraction for ESPET 128x64 SSD1306 OLED

The physical OLED display uses an 8x16 monospace font:
- 128 pixels / 8 pixels = strictly 16 characters per line.
- 2 lines are rendered during TTS speech (Line 1 at Y=8, Line 2 at Y=32).
- Total visible characters = 32 (2 lines x 16 chars).

This module distills full conversational responses (which are spoken via TTS)
into two punchy, meaningful lines (<= 16 chars each) so important keywords
are visible without word-splitting or truncation.
"""

import re
import unicodedata
from typing import Tuple

MAX_LINE_CHARS = 16

# Emojis and pictographs regex
_EMOJI_RE = re.compile(
    "[\U0001F000-\U0001FAFF\U00002600-\U000027BF\U0001F900-\U0001F9FF\u200d\ufe0f]+",
    flags=re.UNICODE,
)

# Common conversational filler prefixes to strip
_FILLER_PREFIXES = [
    r"^(?:sure|okay|ok|certainly|absolutely|alright|yes|no)[,!.\s]+",
    r"^(?:here\s+is|here\s+are|here\'s)[,\s]+",
    r"^(?:i\s+have|i\'ve|i\s+can|i\s+will|i\s+already)\s+(?:successfully\s+)?",
    r"^(?:as\s+requested|as\s+you\s+asked|per\s+your\s+request)[,\s]+",
    r"^(?:hello|hi|hey|greetings)[,!.\s]+",
    r"^(?:the\s+system\s+has|the\s+device\s+has)\s+",
]

# Stopwords to discard when compressing general prose
_STOPWORDS = {
    "a", "an", "the", "and", "or", "but", "if", "because", "as", "what",
    "which", "this", "that", "these", "those", "then", "just", "so", "than",
    "such", "both", "through", "about", "for", "is", "of", "while", "during",
    "to", "from", "in", "out", "on", "off", "over", "under", "again", "further",
    "then", "once", "here", "there", "when", "where", "why", "how", "all",
    "any", "both", "each", "few", "more", "most", "other", "some", "such",
    "no", "nor", "not", "only", "own", "same", "so", "than", "too", "very",
    "s", "t", "can", "will", "just", "don", "should", "now", "i", "me", "my",
    "myself", "we", "our", "ours", "ourselves", "you", "your", "yours",
    "yourself", "yourselves", "he", "him", "his", "himself", "she", "her",
    "hers", "herself", "it", "its", "itself", "they", "them", "their",
    "theirs", "themselves", "am", "are", "was", "were", "be", "been", "being",
    "have", "has", "had", "having", "do", "does", "did", "doing", "would",
    "could", "please", "thank", "thanks", "let", "know",
}


def _clean_text(text: str, strip_fillers: bool = True) -> str:
    """Strip emojis, markdown formatting, newlines, and non-ASCII characters."""
    if not text:
        return ""

    # Replace newlines with spaces
    text = text.replace("\r", " ").replace("\n", " ")

    # Strip emojis
    text = _EMOJI_RE.sub(" ", text)

    # Strip markdown: **bold**, *italic*, `code`, # headers, [links](...)
    text = re.sub(r"\*\*([^*]+)\*\*", r"\1", text)
    text = re.sub(r"\*([^*]+)\*", r"\1", text)
    text = re.sub(r"`([^`]+)`", r"\1", text)
    text = re.sub(r"^#+\s*", "", text)
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)

    # Normalize unicode (e.g. ° -> C or deg)
    text = text.replace("°C", "C").replace("℃", "C").replace("°F", "F")
    text = text.replace("°", "")

    # Replace quotes and dashes with ASCII equivalents
    text = text.replace("“", "\"").replace("”", "\"").replace("‘", "'").replace("’", "'")
    text = text.replace("—", "-").replace("–", "-")

    # Normalize accents to plain ASCII
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode("ascii")

    # Strip conversational prefixes if requested
    if strip_fillers:
        prev = ""
        while prev != text:
            prev = text
            for pat in _FILLER_PREFIXES:
                text = re.sub(pat, "", text, flags=re.IGNORECASE).strip()

    return re.sub(r"\s+", " ", text).strip()


def _wrap_words(words: list[str]) -> Tuple[str, str]:
    """Fit words into 2 lines <= 16 characters each, without splitting words."""
    l1_parts: list[str] = []
    l2_parts: list[str] = []

    for w in words:
        if not w:
            continue
        # Truncate single word if word itself exceeds 16 chars
        if len(w) > MAX_LINE_CHARS:
            w = w[:MAX_LINE_CHARS]

        test_l1 = " ".join(l1_parts + [w]) if l1_parts else w
        if len(test_l1) <= MAX_LINE_CHARS:
            l1_parts.append(w)
            continue

        test_l2 = " ".join(l2_parts + [w]) if l2_parts else w
        if len(test_l2) <= MAX_LINE_CHARS:
            l2_parts.append(w)
            continue

        # Both lines full
        break

    return (" ".join(l1_parts)[:MAX_LINE_CHARS], " ".join(l2_parts)[:MAX_LINE_CHARS])


def extract_oled_keywords(text: str) -> Tuple[str, str]:
    """
    Distills conversational text into two lines (<= 16 chars each) for SSD1306 OLED.
    Returns:
        (line1, line2) where len(line1) <= 16 and len(line2) <= 16.
    """
    if not text or not text.strip():
        return ("", "")

    # Check if raw cleaned text already fits in 1 line
    raw_clean = _clean_text(text, strip_fillers=False)
    if len(raw_clean) <= MAX_LINE_CHARS:
        return (raw_clean, "")

    cleaned = _clean_text(text, strip_fillers=True)
    if not cleaned:
        cleaned = raw_clean
    if len(cleaned) <= MAX_LINE_CHARS:
        return (cleaned, "")

    lower = cleaned.lower()

    # 2. Hardware: LED controls
    if "led" in lower:
        # Determine LED color/type
        if "white" in lower:
            led_type = "White LED"
        elif "status" in lower:
            led_type = "Status LED"
        elif "rgb" in lower:
            led_type = "RGB LED"
        else:
            led_type = "LED"

        pin_match = re.search(r"gpio\s*(\d+)|pin\s*(\d+)", lower)
        pin_str = f"P{pin_match.group(1) or pin_match.group(2)}" if pin_match else ""

        if any(w in lower for w in ["turned on", "turn on", "on", "high", "enabled"]):
            state = f"{pin_str} ON".strip() if pin_str else "State: ON"
            return (led_type[:MAX_LINE_CHARS], state[:MAX_LINE_CHARS])
        elif any(w in lower for w in ["turned off", "turn off", "off", "low", "disabled"]):
            state = f"{pin_str} OFF".strip() if pin_str else "State: OFF"
            return (led_type[:MAX_LINE_CHARS], state[:MAX_LINE_CHARS])

    # 3. Hardware: Camera / Snapshot
    if any(k in lower for k in ["photo", "camera", "picture", "snapshot", "image"]):
        if any(k in lower for k in ["telegram", "sent", "uploaded"]):
            return ("Photo Captured", "Sent to Telegram")
        return ("Camera Snapshot", "Captured OK")

    # 4. Hardware: GPIO Read / Write
    gpio_match = re.search(r"gpio\s*(\d+)", lower)
    if gpio_match:
        gpio_num = gpio_match.group(1)
        if "high" in lower or " 1" in lower or "state is 1" in lower or "set to 1" in lower:
            return (f"GPIO {gpio_num}", "State: HIGH")
        if "low" in lower or " 0" in lower or "state is 0" in lower or "set to 0" in lower:
            return (f"GPIO {gpio_num}", "State: LOW")
        return (f"GPIO {gpio_num}", "Action Executed"[:MAX_LINE_CHARS])

    # 5. Weather / Forecast
    if any(k in lower for k in ["weather", "temperature", "forecast", "degree", "sunny", "rain", "cloudy"]):
        # Look for temperature pattern (e.g. 28C, 28 C, 28 degrees)
        temp_match = re.search(r"(\d{1,2})\s*(?:c|degrees|f)?", lower)
        temp_str = f"{temp_match.group(1)}C" if temp_match else ""

        # Condition keyword
        cond = ""
        for c in ["Rain", "Sunny", "Cloudy", "Storm", "Thunderstorm", "Clear", "Drizzle", "Humid"]:
            if c.lower() in lower:
                cond = c
                break

        # City / location search (common cities or pattern after 'in')
        loc_match = re.search(r"\bin\s+([A-Za-z\s]+?)(?:\s+is|\s+today|,|\.|$)", cleaned, re.IGNORECASE)
        loc = loc_match.group(1).strip() if loc_match else ""
        if len(loc) > MAX_LINE_CHARS:
            loc = loc.split()[0]  # First word of city if too long

        if loc and (temp_str or cond):
            second_line = f"{temp_str} {cond}".strip() if (temp_str and cond) else (temp_str or cond)
            return (loc[:MAX_LINE_CHARS], second_line[:MAX_LINE_CHARS])
        elif temp_str or cond:
            second_line = f"{temp_str} {cond}".strip() if (temp_str and cond) else (temp_str or cond)
            return ("Weather Report", second_line[:MAX_LINE_CHARS])

    # 6. Time / Date / Scheduling
    time_match = re.search(r"\b(\d{1,2}:\d{2}(?::\d{2})?\s*(?:am|pm)?)\b", lower)
    if "time" in lower and time_match:
        return ("Current Time", time_match.group(1).upper()[:MAX_LINE_CHARS])

    if any(k in lower for k in ["reminder", "calendar", "event", "schedule", "cron"]):
        if "meeting" in lower:
            time_part = time_match.group(1).upper() if time_match else "Scheduled"
            return ("Meeting Alert", time_part[:MAX_LINE_CHARS])
        return ("Schedule Alert", "Updated"[:MAX_LINE_CHARS])

    # 7. Colon-separated results (e.g. "Status: Completed successfully")
    if ":" in cleaned:
        parts = cleaned.split(":", 1)
        left = parts[0].strip()
        right = parts[1].strip()
        if len(left) <= MAX_LINE_CHARS:
            # Wrap right side
            r_words = [w for w in right.split() if w.lower() not in _STOPWORDS] or right.split()
            l2 = " ".join(r_words)[:MAX_LINE_CHARS].strip()
            if l2:
                return (left, l2)

    # 8. General prose fallback:
    # Filter stopwords, prioritize meaningful nouns and verbs, and wrap cleanly
    all_words = re.findall(r"[A-Za-z0-9]+", cleaned)
    filtered = [w for w in all_words if w.lower() not in _STOPWORDS]

    # If stopword filtering made it too short or empty, use original words
    candidate_words = filtered if len(filtered) >= 2 else all_words

    l1, l2 = _wrap_words(candidate_words)

    # Guarantee non-empty l1 if cleaned was non-empty
    if not l1 and cleaned:
        l1 = cleaned[:MAX_LINE_CHARS].strip()

    return (l1[:MAX_LINE_CHARS], l2[:MAX_LINE_CHARS])
