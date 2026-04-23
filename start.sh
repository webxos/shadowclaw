#!/usr/bin/env bash
# Shadowclaw Startup Script
# Location: /home/kali/shadowclaw-v2/start.sh

set -e

# ---------- Configuration ----------
PROGRAM_NAME="shadowclaw"
PROGRAM_PATH="./$PROGRAM_NAME"
STATE_FILE="shadowclaw.bin"
DATA_DIR="shadowclaw_data"
SOUL_FILE="$DATA_DIR/shadowsoul.md"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# ---------- Helper Functions ----------
print_status() {
    echo -e "${CYAN}[*]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

# ---------- Dependency Checks ----------
print_status "Checking dependencies..."

# Check for curl (libcurl development files already needed at compile time)
if ! command -v curl &> /dev/null; then
    print_error "curl not found. Please install curl (sudo apt install curl)."
    exit 1
fi

# Check for Ollama (optional, but warn)
if ! command -v ollama &> /dev/null; then
    print_warning "Ollama not found in PATH. The agent will run in --no-llm mode if Ollama is not reachable."
else
    # Check if Ollama service is running
    if ! curl -s http://localhost:11434/api/tags > /dev/null 2>&1; then
        print_warning "Ollama service not responding. Try 'ollama serve' or start the service."
    else
        print_success "Ollama is reachable."
    fi
fi

# ---------- Binary Existence & Permissions ----------
cd "$(dirname "$0")" || { print_error "Failed to change to script directory."; exit 1; }

if [ ! -f "$PROGRAM_PATH" ]; then
    print_error "Binary '$PROGRAM_NAME' not found in current directory."
    print_status "Compiling Shadowclaw..."
    if [ -f "shadowclaw.c" ] && [ -f "interpreter.c" ] && [ -f "cJSON.c" ]; then
        make clean 2>/dev/null || true
        if make; then
            print_success "Compilation successful."
        else
            print_error "Compilation failed. Check dependencies (libcurl, libpthread, readline)."
            exit 1
        fi
    else
        print_error "Source files missing. Please ensure shadowclaw.c, interpreter.c, cJSON.c are present."
        exit 1
    fi
fi

if [ ! -x "$PROGRAM_PATH" ]; then
    print_warning "Binary not executable. Fixing permissions..."
    chmod +x "$PROGRAM_PATH"
fi

# ---------- Environment Setup ----------
# Ensure data directory exists
if [ ! -d "$DATA_DIR" ]; then
    mkdir -p "$DATA_DIR"
    print_success "Created data directory: $DATA_DIR"
fi

# Optionally set environment variables for timeouts (uncomment to override)
# export SHADOWCLAW_CONNECT_TIMEOUT=15
# export SHADOWCLAW_TOTAL_TIMEOUT=180
# export SHADOWCLAW_RETRY_ATTEMPTS=3

# ---------- Display Information ----------
clear
echo -e "${BOLD}╔════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║        Shadowclaw AI Agent v3.2           ║${NC}"
echo -e "${BOLD}╚════════════════════════════════════════════╝${NC}"
echo ""
print_status "Working directory: $(pwd)"
print_status "State file: $STATE_FILE"
print_status "Soul memory: $SOUL_FILE"
echo ""

# ---------- Launch ----------
print_success "Launching Shadowclaw..."
echo ""

# Pass any command-line arguments to the program
exec "$PROGRAM_PATH" "$@"
