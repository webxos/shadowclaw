# Shadowclaw – Version 3.3

![Version](https://img.shields.io/badge/version-3.2-blue)
![C](https://img.shields.io/badge/language-C-blue)
![License](https://img.shields.io/badge/license-MIT-green)

![Alt Text](https://github.com/webxos/shadowclaw/blob/main/assets/shadowclaw.jpeg)

**Shadowclaw** is a lightweight, self-contained AI agent written in C. It uses a local LLM (Ollama) to reason and plan, executes tools (file I/O, HTTP, shell, cron, webhooks), and persistently stores all memories – conversation, skills, cron jobs, and core knowledge – in a custom **arena** plus a human‑readable **soul file**. The agent follows a ReWOO‑style plan‑and‑solve pattern with full tool integration.

---

## Features

- **🧠 Local LLM integration** – works with any Ollama model (default: `tinyllama:1.1b`).
- **🔧 Built‑in tools** – `file_read`, `file_write`, `http_get`, `math`, `list_dir`, `shell` (disabled by default), and more.
- **⏰ Cron jobs** – schedule recurring tasks using `@every N[s/m/h]`, `@hourly`, `@daily`, `@weekly`.
- **🌐 Webhooks** – trigger HTTP POST calls on tool execution or cron events.
- **🎓 Dynamic skills** – create reusable multi‑step workflows without recompiling.
- **💾 Core memory** – persistent key‑value storage (JSON) that survives across sessions.
- **📜 Soul file** – Markdown export of all memories (conversation, skills, crons, webhooks, core memory).
- **🎨 Colored TUI** – optional GNU readline support for line editing and history.
- **⚡ Thread‑safe** – cron jobs run in a separate thread, tool calls are queued.
- **🛡️ Security** – path sandboxing, domain allowlist, shell opt‑in, dry‑run mode.

---

## 📦 Requirements

- **Linux / macOS / WSL** (tested on Ubuntu 22.04, Kali)
- **Ollama** (running locally) – optional, the agent can run in `--no-llm` mode
- **Dependencies**:
  - `libcurl` (HTTP requests)
  - `libpthread` (threading)
  - `libreadline` (optional, for TUI enhancements)
  - `gcc` or `clang` with C99 support

---

## Installation + Launch

*Put all files into a single folder on your system*

```bash
cd ~/shadowclaw (The folder you put the files in)
make clean && make
./start.sh
```

Command‑line flags:

| Flag | Effect |
|------|--------|
| `--no-llm` | Disable LLM calls, use only interpreter commands. |
| `--dry-run` | Simulate tool execution (no actual file/HTTP actions). |
| `--log <file>` | Append logs to a file. |
| `-f <file>` | Use an alternative state file (default: `shadowclaw.bin`). |

### First start

- The agent creates `shadowclaw.bin` (binary state) and a folder `shadowclaw_data/` containing `shadowsoul.md`.
- If Ollama is not reachable, it automatically falls back to `--no-llm` mode.
- A default heartbeat cron job (`@every 120s`) is added automatically to keep the soul file updated.

---

## Interactive Commands

Shadowclaw understands both natural language (sent to the LLM) and slash commands.

| Command | Description |
|---------|-------------|
| `/help` | Show help and list all commands. |
| `/tools` | List available built‑in tools. |
| `/state` | Show arena memory usage and soul file stats. |
| `/clear` | Erase conversation history (keeps system prompt and core memory). |
| `/exit` | Quit the agent. |
| `/loop <schedule> <tool> [args]` | Schedule a recurring task. Examples:<br>`/loop 30m http_get https://example.com`<br>`/loop daily math "1+1"` |
| `/crons` | List all scheduled cron jobs. |
| `/webhooks` | Show registered webhooks. |
| `/skills` | List dynamic skills. |
| `/compact` | Manually compact the arena (remove deleted blobs). |
| `/soul` | Display information about `shadowsoul.md`. |

---

## 🛠️ Tools

Tools are invoked by the LLM during the “plan” phase. Each tool is described in the LLM prompt with its parameters and an example.

| Tool | Description | Example args |
|------|-------------|---------------|
| `file_read` | Read a file (max 10 MB, path must be inside CWD). | `notes.txt` |
| `file_write` | Write content to a file (overwrites). | `output.txt Hello world` |
| `http_get` | HTTP GET to an allowed domain (see `allowed_domains` in source). | `https://example.com/data` |
| `math` | Evaluate arithmetic expression. | `(2+3)*4` |
| `list_dir` | List directory contents. | `.` or `/home/user` |
| `webhook_add` | Register a webhook (JSON: `{"url":"...","event":"..."}`). | `{"url":"http://...","event":"tool:http_get"}` |
| `cron_add` | Add a cron job (JSON: `{"schedule":"...","tool":"...","args":"..."}`). | `{"schedule":"@every 30m","tool":"math","args":"1+1"}` |
| `cron_list` | List all cron jobs. | (none) |
| `cron_remove` | Remove cron jobs containing a substring in their JSON representation. | `@every` |
| `skill_add` | Create a dynamic skill (JSON with `name`, `desc`, `steps` array, optionally `interpreter_command`). | See below. |
| `skill_run` | Run a skill by name. | `weather London` |
| `list_skills` | List all available skills. | (none) |
| `update_core_memory` | Merge JSON object into core memory. | `{"user_name":"Alice","preferences":{"theme":"dark"}}` |
| `recall` | Search conversation history for a keyword. | `project` |
| `heartbeat` | Internal (used by cron). | (none) |

> **Security:** The `shell` tool is compiled out by default. To enable it, add `-DENABLE_SHELL_TOOL` to `CFLAGS` and understand the risks.

---

## Dynamic Skills

Skills are sequences of tool calls stored in the arena as `BLOB_KIND_SKILL`. Example creation:

```json
{
  "name": "weather",
  "desc": "Get weather for a city",
  "steps": [
    {"tool": "http_get", "args": "https://wttr.in/{0}"},
    {"tool": "file_write", "args": "/tmp/weather.txt {result}"}
  ]
}
```

Placeholders supported:
- `{args}` – the whole argument string passed to `skill_run`
- `{0}`, `{1}`, … – positional arguments (split by spaces)
- `{result}` – output of the previous step

Skills can also delegate to an external interpreter command (e.g., a Python script) via the optional `interpreter_command` field.

---

## Soul File

All persistent memories are written to `shadowclaw_data/shadowsoul.md` in Markdown format. It contains:

- `## Core Memory` – JSON key‑value store.
- `## Skills` – list of registered skills (JSON).
- `## Cron Jobs` – all scheduled jobs.
- `## Webhooks` – registered webhooks.
- `## Conversation Log` – user, assistant, tool calls, and results.

The file is updated every 5 writes (write‑behind) and immediately after important events.

---

## ⚙️ Configuration via Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `SHADOWCLAW_CONNECT_TIMEOUT` | 10 | Seconds to wait for Ollama connection. |
| `SHADOWCLAW_TOTAL_TIMEOUT` | 120 | Total LLM request timeout (increased on retries). |
| `SHADOWCLAW_RETRY_ATTEMPTS` | 3 | Number of retries with exponential backoff. |

---

## 📁 Project Structure

```
shadowclaw/
├── shadowclaw.c          # Main program, arena, tools, cron, LLM
├── interpreter.c         # Local command interpreter (used in --no-llm mode)
├── interpreter.h         # Header for interpreter
├── cJSON.c / cJSON.h     # JSON library
├── Makefile              # Build instructions
├── start.sh              # Helper startup script (checks dependencies)
└── README.md             # This file
```

## 📄 License

MIT License. 

## Archive

Version 1.3: https://github.com/webxos/webXOS/tree/main/shadowclaw
