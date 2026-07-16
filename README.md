# Shadowclaw – Version 3.4 (Under Development and Testing)

![Version](https://img.shields.io/badge/version-3.2-blue)
![C](https://img.shields.io/badge/language-C-blue)
![License](https://img.shields.io/badge/license-MIT-green)

![Alt Text](https://github.com/webxos/shadowclaw/blob/main/assets/shadowclaw.jpeg)

<div style="max-width: 100%; overflow-x: auto; background: #f6f8fa; padding: 16px; border-radius: 8px;">
  <pre style="font-family: 'Courier New', monospace; font-size: clamp(12px, 2vw, 18px); line-height: 1.2; margin: 0;">
  ____  _               _                   _
 / ___|| |__   __ _  __| | _____      _____| | __ ___      __
 \___ \| '_ \ / _` |/ _` |/ _ \ \ /\ / / __| |/ _` \ \ /\ / /
  ___) | | | | (_| | (_| | (_) \ V  V / (__| | (_| |\ V  V /
 |____/|_| |_|\__,_|\__,_|\___/ \_/\_/ \___|_|\__,_| \_/\_/
  </pre>
</div>

**Shadowclaw** is a lightweight, self-contained AI agent harness written in C. It uses a local LLM (Ollama) to reason and plan, executes tools (file I/O, HTTP, shell, cron, webhooks), and persistently stores all memories – conversation, skills, cron jobs, and core knowledge – in a custom **arena** plus a human‑readable **soul file**. The agent follows a ReWOO‑style plan‑and‑solve pattern with full tool integration.

---

## Features

- Local LLM integration – works with any Ollama model (default: `tinyllama:1.1b`).
- Built‑in tools – `file_read`, `file_write`, `http_get`, `math`, `list_dir`, `shell` (disabled by default), and more.
- Cron Jobs – schedule recurring tasks using `@every N[s/m/h]`, `@hourly`, `@daily`, `@weekly`.
- Webhooks – trigger HTTP POST calls on tool execution or cron events.
- Dynamic skills – create reusable multi‑step workflows without recompiling.
- Core memory – persistent key‑value storage (JSON) that survives across sessions.
- Soul file – Markdown export of all memories (conversation, skills, crons, webhooks, core memory).
- Colored TUI – optional GNU readline support for line editing and history.
- Thread‑safe – cron jobs run in a separate thread, tool calls are queued.
- Security – path sandboxing, domain allowlist, shell opt‑in, dry‑run mode.

---

## Requirements

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

## Setup your local Ollama model

*Shadowclaw is set to use qwen2.5:0.5b as a default, to change this:*

Find line 633 in the shadowclaw.c file:

```bash
static const char *ollama_endpoint = "http://localhost:11434";
static const char *ollama_model = "qwen2.5:0.5b"; (Change this to desired model)
static long llm_connect_timeout = 15;
```

Also line 16 in the start.sh file:

```bash
OLLAMA_ENDPOINT="${OLLAMA_ENDPOINT:-http://localhost:11434}"
OLLAMA_MODEL="${OLLAMA_MODEL:-qwen2.5:0.5b}" (Change this to desired model)
```

---

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

## Tools

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

## Configuration via Environment Variables

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

The architectural framework of the Shadowclaw harness breaks down into five distinct subsystems:

```
                  ┌──────────────────────────────────────────┐
                  │          SHADOWCLAW C HARNESS            │
                  │                                          │
                  │  ┌──────────────┐      ┌──────────────┐  │
 ┌──────────┐     │  │  1. Memory   │      │  2. Tool     │  │     ┌───────────┐
 │   User   ├────►│  │   (Arena &   │      │  Dispatcher  │  │◄───►│ Local LLM │
 │ Interface│     │  │  Soul File)  │      │(File, Shell) │  │     │ (Ollama)  │
 └──────────┘     │  └──────┬───────┘      └──────┬───────┘  │     └───────────┘
                  │         │                     │          │
                  │         ▼                     ▼          │
                  │  ┌────────────────────────────────────┐  │
                  │  │     3. The Orchestration Loop      │  │
                  │  │       (Plan ➔ Act ➔ Observe)       │  │
                  │  └────────────────────────────────────┘  │
                  │  ┌──────────────┐      ┌──────────────┐  │
                  │  │ 4. Transmit  │      │  5. Security │  │
                  │  │ (curl/cJSON) │      │  (Sandbox)   │  │
                  │  └──────────────┘      └──────────────┘  │
                  └──────────────────────────────────────────┘
```

## 1. Memory Subsystem (The Arena & The Soul File)
Instead of querying an external database, Shadowclaw structures its memory layout using a growable shadow arena. 

* Contiguous Memory Block: Everything—current conversations, global configurations, tool parameters, and historical logs—is allocated in one continuous memory buffer using custom headers. 
* State Persistence: When saving, the harness performs a raw memory dump of this arena directly into a single binary file (shadowclaw.bin). 
* The "Soul File": To make things human-readable, the harness automatically mirrors and exports state into a Markdown file (the "soul file"), capturing persistent core memories, skills, and cron schedules. 

## 2. The Tool Dispatcher
The model can only output text. The harness converts text intents into system actions. Shadowclaw includes native, hardcoded handlers for basic system tools: 

* File I/O: Reading, editing, and mapping local workspace directories.
* Bash Execution: A secure pipe that evaluates terminal commands.
* Math Parser: Outsources complex evaluation to native utilities like bc.
* Dynamic Workflows: A feature that allows the model to register reusable multi-step macros ("dynamic skills") into the memory arena without requiring a program recompilation.

## 3. The Orchestration Loop (Plan-Act-Observe)
This loop drives the entire runtime lifecycle. It repeatedly manages the control token sequence: 

   1. Listen: Receives input via the Command Line interface / TUI. 
   2. Context Assembly: Fetches history from the memory arena, appends system rules, and binds tool definitions into a final raw string prompt. 
   3. Inference Call: Handshakes with the local model. 
   4. Regex Execution: If the model returns a structured tool signature, the loop pauses, executes the requested action via the Tool Dispatcher, collects the output, and appends it as an "observation" back into the prompt buffer. 
   5. Termination: The loop breaks only when the model returns a final text answer instead of another tool request. 

## 4. The Network and Serialization Layer (JSON Glue)
Because it avoids bulkier runtime dependencies, the communication infrastructure is written down to the metal using two foundational libraries: 

* libcurl Connection: The harness uses C-native libcurl to handle streaming and asynchronous POST payloads targeting Ollama's local HTTP API endpoints (localhost:11434).  
* cJSON Parsing: Since LLM microservices interface purely via text representations, cJSON handles text-to-object conversions, unpacking nested tool structures, and encoding tool payloads within strict bounds. 

## 5. Security & Isolation Boundaries
A primary risk of full-automation agents is untrusted shell access. Shadowclaw manages this threat on a compile/flag layer: 

* Path Sandboxing: Prevents file modifications outside of the pre-declared workspace directory.
* Shell Opt-in / Dry Run: Allows the user to toggle absolute shell blockades or test model output safety before system modifications execute.

------------------------------
## Shadowclaw vs. OpenClaw: Harness Weight
The engineering trade-offs between the two frameworks map as follows:

| Harness Feature | OpenClaw (TypeScript/Docker) | Shadowclaw (C Binary) |
|---|---|---|
| Footprint | High (Node.js runtime, virtual environments) | Low (Single static executable) |
| Concurrency | Asynchronous Event Loop / Node Worker Threads | Native libpthread (Tool queues & cron threads) |
| Tool Protocol | Full Model Context Protocol (MCP) | Inline Hardcoded Array Mapping |
| Memory | Database/File system structures (MEMORY.md) | Flat Arena Buffers (shadowclaw.bin) |

---

## Hugging Face

 [https://huggingface.co](https://huggingface.co/webxos/shadowclaw-c)

## 📄 License

MIT License. 

## Archive

Version 1.3: https://github.com/webxos/webXOS/tree/main/shadowclaw

## SCREENSHOTS

![Alt Text](https://github.com/webxos/shadowclaw/blob/main/assets/screen1.png)

![Alt Text](https://github.com/webxos/shadowclaw/blob/main/assets/screen2.png)
