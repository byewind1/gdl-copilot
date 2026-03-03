from __future__ import annotations

import os
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse
from pydantic import BaseModel, Field

OBR_ROOT = Path("/Users/ren/MAC工作/工作/code/开源项目/gdl-agent")
OBR_CONFIG_PATH = OBR_ROOT / "config.toml"
if str(OBR_ROOT) not in sys.path:
    sys.path.insert(0, str(OBR_ROOT))

from openbrep.config import GDLAgentConfig
from openbrep.llm import LLMAdapter

app = FastAPI(title="GDL Copilot", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

SYSTEM_PROMPT = """你是 Archicad GDL 脚本 AI 修复助手。
用户会粘贴编译报错或出问题的代码片段。
任务：
1. 一句话说清楚问题原因
2. 给出可直接粘回 Archicad GDL 编辑器的修复代码
3. 代码用 ```gdl ``` 包裹
不废话，建筑师只要能跑的代码。若信息不足主动追问。"""

clipboard_lock = threading.Lock()
clipboard_buffer: list[str] = []
clipboard_last_raw = ""


def _read_clipboard_text() -> str:
    try:
        return subprocess.check_output(["pbpaste"], text=True, timeout=1).strip()
    except Exception:
        return ""


def _clipboard_watch_loop() -> None:
    global clipboard_last_raw

    while True:
        value = _read_clipboard_text()
        if value and value != clipboard_last_raw:
            clipboard_last_raw = value
            with clipboard_lock:
                if value not in clipboard_buffer:
                    clipboard_buffer.append(value)
        time.sleep(0.8)


threading.Thread(target=_clipboard_watch_loop, daemon=True).start()


class HistoryMessage(BaseModel):
    role: str
    content: str


class ImageItem(BaseModel):
    b64: str = Field(..., min_length=1)
    mime: str | None = None


class ChatRequest(BaseModel):
    message: str = Field(..., min_length=1)
    history: list[HistoryMessage] = Field(default_factory=list)
    images: list[ImageItem] | None = None


class ChatResponse(BaseModel):
    reply: str
    code_blocks: list[str]


class ClipboardBufferResponse(BaseModel):
    items: list[str]


class ClipboardBufferUpdateRequest(BaseModel):
    items: list[str] = Field(default_factory=list)


def _extract_gdl_code_blocks(text: str) -> list[str]:
    pattern = re.compile(r"```gdl\s*(.*?)\s*```", re.IGNORECASE | re.DOTALL)
    return [m.group(1).strip() for m in pattern.finditer(text)]


def _build_messages(req: ChatRequest) -> list[dict[str, object]]:
    messages: list[dict[str, object]] = [{"role": "system", "content": SYSTEM_PROMPT}]

    for item in req.history:
        role = item.role if item.role in {"user", "assistant"} else "user"
        content = item.content.strip()
        if content:
            messages.append({"role": role, "content": content})

    valid_images = [img for img in (req.images or []) if img.b64.strip()]
    if valid_images:
        user_content: list[dict[str, str | dict[str, str]]] = []
        for img in valid_images:
            user_content.append({
                "type": "image_url",
                "image_url": {"url": f"data:{img.mime or 'image/png'};base64,{img.b64.strip()}"},
            })
        user_content.append({"type": "text", "text": req.message.strip()})
        messages.append({"role": "user", "content": user_content})
    else:
        messages.append({"role": "user", "content": req.message.strip()})

    return messages


def _create_llm_adapter() -> LLMAdapter:
    config_path = os.environ.get("GDL_AGENT_CONFIG")
    if config_path is None:
        config_path = str(OBR_CONFIG_PATH)

    cfg = GDLAgentConfig.load(config_path)
    return LLMAdapter(cfg.llm)


@app.get("/", response_class=HTMLResponse)
def index() -> HTMLResponse:
    html_path = Path(__file__).with_name("index.html")
    return HTMLResponse(content=html_path.read_text(encoding="utf-8"))


@app.get("/clipboard-buffer", response_model=ClipboardBufferResponse)
def get_clipboard_buffer() -> ClipboardBufferResponse:
    with clipboard_lock:
        return ClipboardBufferResponse(items=list(clipboard_buffer))


@app.post("/clipboard-buffer/clear", response_model=ClipboardBufferResponse)
def clear_clipboard_buffer(req: ClipboardBufferUpdateRequest | None = None) -> ClipboardBufferResponse:
    items = req.items if req is not None else []
    normalized = [item.strip() for item in items if item and item.strip()]
    with clipboard_lock:
        clipboard_buffer.clear()
        clipboard_buffer.extend(normalized)
        return ClipboardBufferResponse(items=list(clipboard_buffer))


@app.post("/chat", response_model=ChatResponse)
def chat(req: ChatRequest) -> ChatResponse:
    llm = _create_llm_adapter()
    messages = _build_messages(req)
    resp = llm.generate(messages)

    reply = (resp.content or "").strip()
    code_blocks = _extract_gdl_code_blocks(reply)

    return ChatResponse(reply=reply, code_blocks=code_blocks)
