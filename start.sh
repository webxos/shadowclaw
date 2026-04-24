#!/usr/bin/env bash
# Shadowclaw Startup Script v3.3+ (Fixed)
# Location: /home/kali/shadowclaw-v2/start.sh

set -e

# ---------- Configuration ----------
PROGRAM_NAME="shadowclaw"
PROGRAM_PATH="./$PROGRAM_NAME"
STATE_FILE="shadowclaw.bin"
DATA_DIR="shadowclaw_data"
SOUL_FILE="$DATA_DIR/shadowsoul.md"

# Ollama settings (can be overridden by environment)
OLLAMA_ENDPOINT="${OLLAMA_ENDPOINT:-http://localhost:11434}"
OLLAMA_MODEL="${OLLAMA_MODEL:-tinyllama:1.1b}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# ---------- Helper Functions ----------
print_status() { echo -e "${CYAN}[*]${NC} $1"; }
print_success() { echo -e "${GREEN}[✓]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[!]${NC} $1"; }
print_error() { echo -e "${RED}[✗]${NC} $1"; }

# ---------- Environment Variables for Timeouts (uncommented & realistic) ----------
export SHADOWCLAW_CONNECT_TIMEOUT="${SHADOWCLAW_CONNECT_TIMEOUT:-15}"
export SHADOWCLAW_TOTAL_TIMEOUT="${SHADOWCLAW_TOTAL_TIMEOUT:-180}"
export SHADOWCLAW_RETRY_ATTEMPTS="${SHADOWCLAW_RETRY_ATTEMPTS:-5}"

print_status "Timeout settings: connect=${SHADOWCLAW_CONNECT_TIMEOUT}s, total=${SHADOWCLAW_TOTAL_TIMEOUT}s, retries=${SHADOWCLAW_RETRY_ATTEMPTS}"

# ---------- Dependency Checks ----------
print_status "Checking dependencies..."

if ! command -v curl &> /dev/null; then
    print_error "curl not found. Please install curl (sudo apt install curl)."
    exit 1
fi

# ---------- Ollama Availability & Model Check ----------
NO_LLM_MODE=0
FORCE_LLM=0

# Parse command line arguments for --force-llm
for arg in "$@"; do
    if [ "$arg" = "--force-llm" ]; then
        FORCE_LLM=1
    fi
done

check_ollama() {
    # Quick connectivity test
    if ! curl -s --max-time 2 "$OLLAMA_ENDPOINT/api/tags" > /dev/null 2>&1; then
        return 1
    fi
    # Verify the required model exists
    if ! curl -s "$OLLAMA_ENDPOINT/api/tags" 2>/dev/null | grep -q "\"name\":\"$OLLAMA_MODEL\""; then
        return 2
    fi
    return 0
}

if [ $FORCE_LLM -eq 1 ]; then
    print_warning "--force-llm flag set: will attempt to use LLM even if checks fail."
    NO_LLM_MODE=0
else
    check_ollama
    case $? in
        0)
            print_success "Ollama is reachable and model '$OLLAMA_MODEL' is available."
            ;;
        1)
            print_warning "Ollama not reachable at $OLLAMA_ENDPOINT. Auto-enabling --no-llm mode."
            NO_LLM_MODE=1
            ;;
        2)
            print_warning "Model '$OLLAMA_MODEL' not found in Ollama. Auto-enabling --no-llm mode."
            print_status "Pull it with: ollama pull $OLLAMA_MODEL"
            NO_LLM_MODE=1
            ;;
    esac
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
if [ ! -d "$DATA_DIR" ]; then
    mkdir -p "$DATA_DIR"
    print_success "Created data directory: $DATA_DIR"
fi

# ---------- Display Information ----------
clear
echo -e "${BOLD}╔════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║        Shadowclaw AI Agent v3.4            ║${NC}"
echo -e "${BOLD}╚════════════════════════════════════════════╝${NC}"
echo ""
print_status "Working directory: $(pwd)"
print_status "State file: $STATE_FILE"
print_status "Soul memory: $SOUL_FILE"
if [ $NO_LLM_MODE -eq 1 ]; then
    print_warning "Starting in NO-LLM mode (local interpreter only)."
else
    print_success "LLM mode enabled (model: $OLLAMA_MODEL)."
fi
echo ""

# ---------- Build final argument list ----------
ARGS=()
if [ $NO_LLM_MODE -eq 1 ]; then
    ARGS+=("--no-llm")
fi
# Pass any remaining original arguments (e.g., --dry-run, --log, -f)
for arg in "$@"; do
    if [ "$arg" != "--force-llm" ]; then
        ARGS+=("$arg")
    fi
done

# ---------- Launch ----------
print_success "Launching Shadowclaw..."
exec "$PROGRAM_PATH" "${ARGS[@]}"
