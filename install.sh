#!/usr/bin/env bash
# ==============================================================================
# CalculiX GraphiX (GLFW Edition) — Universal 1-Liner Installer
#
# 1-Liner Usage:
#   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/carlomontec/CalculiX-GraphiX-GLFW/main/install.sh)"
#
# Supports:
#   - macOS (Apple Silicon arm64 & Intel x86_64)
#   - Linux (x86_64: Ubuntu, Debian, RHEL, Rocky, Fedora, Arch)
# ==============================================================================

set -e

REPO="carlomontec/CalculiX-GraphiX-GLFW"
GITHUB_REPO_URL="https://github.com/${REPO}.git"
DEFAULT_RELEASE_URL="https://github.com/${REPO}/releases/latest/download"
RELEASE_URL="${CGX_DOWNLOAD_BASE_URL:-$DEFAULT_RELEASE_URL}"

# Color codes
BOLD="\033[1m"
GREEN="\033[0;32m"
BLUE="\033[0;34m"
YELLOW="\033[1;33m"
RED="\033[0;31m"
NC="\033[0m" # No Color

# Helper for interactive reading (handles stdin pipe gracefully)
prompt_read() {
    local prompt_msg="$1"
    local default_val="$2"
    local var_name="$3"
    local input_val=""

    if [ -n "$NON_INTERACTIVE" ]; then
        eval "$var_name=\"$default_val\""
        return 0
    fi

    if [ -t 0 ]; then
        read -r -p "$prompt_msg" input_val
    elif [ -c /dev/tty ]; then
        read -r -p "$prompt_msg" input_val < /dev/tty
    else
        input_val="$default_val"
    fi

    input_val="${input_val:-$default_val}"
    eval "$var_name=\"$input_val\""
}

echo -e "${BOLD}${BLUE}=====================================================${NC}"
echo -e "${BOLD}${BLUE}   CalculiX GraphiX (GLFW Edition) — Installer       ${NC}"
echo -e "${BOLD}${BLUE}=====================================================${NC}"

# Target install directory (~/.local/bin)
INSTALL_DIR="${HOME}/.local/bin"
mkdir -p "${INSTALL_DIR}"

# Detect OS and Architecture
OS="$(uname -s)"
ARCH="$(uname -m)"

echo -e "Detected Platform: ${BOLD}${GREEN}${OS} (${ARCH})${NC}"

# Map to GitHub Release asset name
BINARY_NAME=""
if [ "${OS}" = "Darwin" ]; then
    if [ "${ARCH}" = "arm64" ]; then
        BINARY_NAME="cgx_glfw-macos-arm64"
    else
        BINARY_NAME="cgx_glfw-macos-x86_64"
    fi
elif [ "${OS}" = "Linux" ]; then
    if [ "${ARCH}" = "x86_64" ]; then
        BINARY_NAME="cgx_glfw-linux-x86_64"
    else
        echo -e "${RED}Error: Linux '${ARCH}' architecture is not supported yet (future work).${NC}"
        exit 1
    fi
else
    echo -e "${RED}Error: Unsupported operating system '${OS}'.${NC}"
    exit 1
fi

# Function: Ensure Homebrew on macOS
ensure_homebrew_macos() {
    if command -v brew &>/dev/null; then
        return 0
    fi

    # Check common paths
    if [ -x "/opt/homebrew/bin/brew" ]; then
        eval "$(/opt/homebrew/bin/brew shellenv)"
        return 0
    elif [ -x "/usr/local/bin/brew" ]; then
        eval "$(/usr/local/bin/brew shellenv)"
        return 0
    fi

    echo -e "\n${BOLD}${YELLOW}--> Homebrew package manager was not found on your Mac.${NC}"
    echo -e "Homebrew (https://brew.sh) is the standard, free open-source package manager for macOS."
    echo -e "It safely provides the GLFW windowing libraries and graphics tools needed by modern apps.\n"

    prompt_read "Would you like to install Homebrew now automatically? [Y/n] " "Y" INSTALL_BREW

    if [[ "$INSTALL_BREW" =~ ^[Yy]$ ]]; then
        echo -e "\n${BOLD}--> Running official Homebrew installer...${NC}"
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

        if [ -x "/opt/homebrew/bin/brew" ]; then
            eval "$(/opt/homebrew/bin/brew shellenv)"
        elif [ -x "/usr/local/bin/brew" ]; then
            eval "$(/usr/local/bin/brew shellenv)"
        fi

        if command -v brew &>/dev/null; then
            echo -e "${GREEN}[OK] Homebrew successfully installed!${NC}"
        else
            echo -e "${RED}Error: Homebrew was installed but 'brew' is not yet in PATH.${NC}"
            exit 1
        fi
    else
        echo -e "\n${YELLOW}Homebrew is required for GLFW on macOS. Install it at https://brew.sh and re-run this script.${NC}"
        exit 1
    fi
}

# Function: Install Runtime Dependencies
install_runtime_deps() {
    echo -e "\n${BOLD}--> Checking runtime dependencies...${NC}"
    if [ "${OS}" = "Darwin" ]; then
        ensure_homebrew_macos
        if brew list glfw &>/dev/null; then
            echo -e "${GREEN}[OK] GLFW runtime is already installed.${NC}"
        else
            echo "Installing glfw via Homebrew..."
            brew install glfw
        fi
    elif [ "${OS}" = "Linux" ]; then
        if ldconfig -p 2>/dev/null | grep -q "libglfw\.so" || [ -f /lib64/libglfw.so.3 ] || [ -f /usr/lib/libglfw.so.3 ] || [ -f /usr/lib/x86_64-linux-gnu/libglfw.so.3 ] || [ -f /home/linuxbrew/.linuxbrew/lib/libglfw.so ]; then
            echo -e "${GREEN}[OK] GLFW runtime library is already installed.${NC}"
            return 0
        fi

        echo "Installing missing runtime libraries (requires sudo)..."
        if command -v apt-get &>/dev/null; then
            sudo apt-get update -y && sudo apt-get install -y libglfw3 libglu1-mesa
        elif command -v dnf &>/dev/null; then
            sudo dnf install -y glfw mesa-libGLU
        elif command -v pacman &>/dev/null; then
            sudo pacman -S --needed glfw-x11 mesa glu
        else
            echo -e "${YELLOW}Please ensure libglfw3 and OpenGL/Mesa runtime libraries are installed.${NC}"
        fi
    fi
}

# Function: Install Build Toolchain Dependencies
install_build_deps() {
    echo -e "\n${BOLD}--> Checking build dependencies...${NC}"
    if [ "${OS}" = "Darwin" ]; then
        ensure_homebrew_macos
        if ! brew list glfw &>/dev/null; then
            brew install glfw
        fi
        if ! command -v make &>/dev/null; then
            brew install make
        fi
        echo -e "${GREEN}[OK] macOS build tools are ready.${NC}"
    elif [ "${OS}" = "Linux" ]; then
        HAS_COMPILER=0
        HAS_GLFW_DEV=0
        
        if command -v gcc &>/dev/null && command -v g++ &>/dev/null && command -v make &>/dev/null; then
            HAS_COMPILER=1
        fi

        if pkg-config --exists glfw3 2>/dev/null || [ -f /usr/include/GLFW/glfw3.h ] || [ -f /home/linuxbrew/.linuxbrew/include/GLFW/glfw3.h ]; then
            HAS_GLFW_DEV=1
        fi

        if [ "$HAS_COMPILER" -eq 1 ] && [ "$HAS_GLFW_DEV" -eq 1 ]; then
            echo -e "${GREEN}[OK] Compilers (gcc/g++) and GLFW development headers are ready.${NC}"
            return 0
        fi

        echo "Installing missing build tools (requires sudo)..."
        if command -v apt-get &>/dev/null; then
            sudo apt-get update -y && sudo apt-get install -y build-essential libglfw3-dev libgl1-mesa-dev libglu1-mesa-dev git
        elif command -v dnf &>/dev/null; then
            sudo dnf install -y gcc-c++ make glfw-devel mesa-libGL-devel mesa-libGLU-devel git
        elif command -v pacman &>/dev/null; then
            sudo pacman -S --needed base-devel glfw-x11 mesa glu git
        fi
    fi
}

# Function: Fast Install via Pre-built Binary
do_fast_install() {
    if [ -z "${BINARY_NAME}" ]; then
        echo -e "${YELLOW}No pre-built binary for ${OS} ${ARCH}. Switching to build from source.${NC}"
        do_build_install
        return
    fi

    install_runtime_deps

    DOWNLOAD_URL="${RELEASE_URL}/${BINARY_NAME}"
    TARGET_BIN="${INSTALL_DIR}/cgx_glfw"

    echo -e "\n${BOLD}--> Fetching pre-compiled binary from:${NC}"
    echo "    ${DOWNLOAD_URL}"
    
    if curl -fSL --progress-bar -o "${TARGET_BIN}" "${DOWNLOAD_URL}"; then
        chmod +x "${TARGET_BIN}"
        echo -e "${GREEN}[OK] Successfully installed: ${TARGET_BIN}${NC}"
    else
        echo -e "${YELLOW}Notice: Pre-compiled binary not found on GitHub Releases yet (${DOWNLOAD_URL}).${NC}"
        echo -e "${BOLD}--> Automatically falling back to local compilation...${NC}"
        do_build_install
        return
    fi
}

# Function: Compile from Source (supports both local repo or standalone 1-liner clone)
do_build_install() {
    install_build_deps

    SRC_ROOT=""
    # Check if run inside existing cloned repo
    if [ -f "$(pwd)/cgx_2.23/src/Makefile.glfw" ]; then
        SRC_ROOT="$(pwd)"
    elif [ -f "$(dirname "$0")/cgx_2.23/src/Makefile.glfw" ]; then
        SRC_ROOT="$(cd "$(dirname "$0")" && pwd)"
    else
        # 1-Liner mode: Clone repo into ~/.cgx/CalculiX-GraphiX-GLFW
        SOURCE_DIR="${HOME}/.cgx"
        mkdir -p "${SOURCE_DIR}"
        SRC_ROOT="${SOURCE_DIR}/CalculiX-GraphiX-GLFW"
        
        echo -e "\n${BOLD}--> Cloning CalculiX-GraphiX-GLFW into ${SRC_ROOT}...${NC}"
        if [ -d "${SRC_ROOT}/.git" ]; then
            git -C "${SRC_ROOT}" pull --rebase || true
        else
            git clone "${GITHUB_REPO_URL}" "${SRC_ROOT}"
        fi
    fi

    echo -e "\n${BOLD}--> Building CGX (GLFW Edition) with native CPU optimizations...${NC}"
    cd "${SRC_ROOT}/cgx_2.23/src"
    
    NPROC=4
    if command -v nproc &>/dev/null; then
        NPROC=$(nproc)
    elif [ "${OS}" = "Darwin" ] && command -v sysctl &>/dev/null; then
        NPROC=$(sysctl -n hw.ncpu)
    fi

    make -f Makefile.glfw clean 2>/dev/null || true
    make -f Makefile.glfw OPT="-O3 -march=native" -j"${NPROC}"

    # Copy output binary to ~/.local/bin/cgx_glfw
    if [ -f "${SRC_ROOT}/bin/cgx_glfw" ]; then
        cp -f "${SRC_ROOT}/bin/cgx_glfw" "${INSTALL_DIR}/cgx_glfw"
        chmod +x "${INSTALL_DIR}/cgx_glfw"
        echo -e "${GREEN}[OK] Build succeeded! Binary installed at: ${INSTALL_DIR}/cgx_glfw${NC}"
    else
        echo -e "${RED}Error: Build failed. Please check compiler output above.${NC}"
        exit 1
    fi
}

# Function: Configure PATH in Shell Profile
ensure_path_configured() {
    # Check if ~/.local/bin is already in PATH
    if [[ ":$PATH:" == *":$INSTALL_DIR:"* ]]; then
        return 0
    fi

    echo -e "\n${BOLD}--> Configuring PATH environment...${NC}"
    
    SHELL_PROFILE=""
    CURRENT_SHELL="$(basename "${SHELL:-bash}")"

    if [ "$CURRENT_SHELL" = "zsh" ]; then
        SHELL_PROFILE="${HOME}/.zshrc"
    elif [ "$CURRENT_SHELL" = "bash" ]; then
        if [ "${OS}" = "Darwin" ]; then
            SHELL_PROFILE="${HOME}/.bash_profile"
        else
            SHELL_PROFILE="${HOME}/.bashrc"
        fi
    else
        SHELL_PROFILE="${HOME}/.profile"
    fi

    EXPORT_CMD="export PATH=\"${INSTALL_DIR}:\$PATH\""

    if [ -f "$SHELL_PROFILE" ] && grep -q ".local/bin" "$SHELL_PROFILE"; then
        echo -e "${GREEN}[OK] PATH already configured in ${SHELL_PROFILE}${NC}"
    else
        echo "" >> "$SHELL_PROFILE"
        echo "# CalculiX GraphiX PATH" >> "$SHELL_PROFILE"
        echo "$EXPORT_CMD" >> "$SHELL_PROFILE"
        echo -e "${GREEN}[OK] Added ~/.local/bin to ${SHELL_PROFILE}${NC}"
    fi

    export PATH="${INSTALL_DIR}:${PATH}"
}

# Function: Optional Global /usr/local/bin Symlink
prompt_global_install() {
    ensure_path_configured

    # Check if /usr/local/bin exists and prompt in interactive mode
    if [ -z "$NON_INTERACTIVE" ] && [ -d "/usr/local/bin" ]; then
        prompt_read "Would you also like to link 'cgx_glfw' to /usr/local/bin? [Y/n] " "Y" LINK_CHOICE
        if [[ "$LINK_CHOICE" =~ ^[Yy]$ ]]; then
            if [ -w /usr/local/bin ]; then
                ln -sf "${INSTALL_DIR}/cgx_glfw" /usr/local/bin/cgx_glfw
                echo -e "${GREEN}[OK] Linked to /usr/local/bin/cgx_glfw${NC}"
            else
                echo "Requesting sudo to create symlink in /usr/local/bin..."
                sudo ln -sf "${INSTALL_DIR}/cgx_glfw" /usr/local/bin/cgx_glfw 2>/dev/null || true
                echo -e "${GREEN}[OK] Linked to /usr/local/bin/cgx_glfw${NC}"
            fi
        fi
    fi

    echo -e "\n${BOLD}${GREEN}=====================================================${NC}"
    echo -e "${BOLD}${GREEN}   Installation Complete!                         ${NC}"
    echo -e "${BOLD}${GREEN}=====================================================${NC}"
    echo -e "You can now run CGX from anywhere in your terminal:"
    echo -e "    ${BOLD}cgx_glfw <model.frd>${NC}\n"
}

# CLI Argument parsing
CHOICE=""
NON_INTERACTIVE=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --binary|--fast|-b)
            CHOICE="1"
            NON_INTERACTIVE="1"
            shift
            ;;
        --build|--source|-s)
            CHOICE="2"
            NON_INTERACTIVE="1"
            shift
            ;;
        --help|-h)
            echo "Usage: install.sh [OPTION]"
            echo "Options:"
            echo "  --binary, -b    Fast install: Download pre-built binary & install runtime libs"
            echo "  --build,  -s    Power install: Compile locally with native CPU optimizations"
            echo "  --help,   -h    Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1"
            exit 1
            ;;
    esac
done

# If no CLI flag passed, prompt interactively
if [ -z "$CHOICE" ]; then
    echo -e "\nChoose installation method:"
    echo -e "  ${BOLD}1) Fast Install${NC} (Download pre-built binary + install runtime libs) ${YELLOW}[Default]${NC}"
    echo -e "  ${BOLD}2) Build from Source${NC} (Compile locally with -march=native for max performance)"
    echo ""
    prompt_read "Select [1/2] (Default: 1): " "1" CHOICE
fi

case "$CHOICE" in
    1)
        do_fast_install
        prompt_global_install
        ;;
    2)
        do_build_install
        prompt_global_install
        ;;
    *)
        echo -e "${RED}Invalid selection. Exiting.${NC}"
        exit 1
        ;;
esac
