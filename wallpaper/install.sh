#!/bin/bash
set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo -e "${GREEN}xwpbg installer${NC}"
echo ""

# Check sudo availability
HAS_SUDO=false
if command -v sudo &>/dev/null && sudo -n true 2>/dev/null; then
    HAS_SUDO=true
fi

# Install dependencies (only if sudo is available)
if [ "$HAS_SUDO" = true ]; then
    if command -v pacman &>/dev/null; then
        echo "Installing dependencies..."
        sudo pacman -S --needed --noconfirm gcc libx11 libxext libjpeg-turbo libpng ffmpeg
    elif command -v apt-get &>/dev/null; then
        echo "Installing dependencies..."
        sudo apt-get update -qq
        sudo apt-get install -y gcc libx11-dev libxext-dev libjpeg-dev libpng-dev ffmpeg
    else
        echo -e "${YELLOW}Unknown package manager, skipping dependency install.${NC}"
        echo "Make sure these are installed: gcc, libx11, libxext, libjpeg, libpng"
    fi
else
    echo -e "${YELLOW}No sudo access, skipping dependency install.${NC}"
    echo "Assuming required libraries are already available."
fi

# Compile
echo "Compiling..."
mkdir -p "$HOME/.local/bin"
if ! gcc "$SCRIPT_DIR/x11_wp_layer.c" \
        -o "$HOME/.local/bin/xwpbg" \
        -lX11 -lXext -ljpeg -lpng \
        -Wall -Wextra -O2 2>&1; then
    echo -e "${RED}Compilation failed.${NC}"
    echo "Make sure gcc, libx11, libxext, libjpeg and libpng are installed."
    exit 1
fi

# Add to PATH
PATH_LINE='export PATH="$PATH:$HOME/.local/bin"'
for RC in "$HOME/.bashrc" "$HOME/.zshrc"; do
    if [ -f "$RC" ]; then
        if ! grep -q '.local/bin' "$RC"; then
            echo "$PATH_LINE" >> "$RC"
            echo "Added ~/.local/bin to PATH in $RC"
        fi
    fi
done

export PATH="$PATH:$HOME/.local/bin"

echo ""
echo -e "${GREEN}Installation complete!${NC}"
echo ""

# ffmpeg availability notice
if command -v ffmpeg &>/dev/null; then
    echo "Usage:"
    echo "  xwpbg image.jpg               # static wallpaper"
    echo "  xwpbg video.mp4               # animated wallpaper from video"
    echo "  xwpbg video.mp4 --fps 24      # custom fps"
    echo "  xwpbg ~/frames/               # animated wallpaper from image folder"
    echo "  pkill xwpbg                   # stop"
else
    echo -e "${YELLOW}ffmpeg not found.${NC} Video files (.mp4, .gif, etc.) cannot be used directly."
    echo ""
    echo "Extract frames using ffmpeg or search 'video to frames extractor' in your browser."
    echo "Download the frames, put them in a folder (e.g. ~/frames/) and run:"
    echo ""
    echo "  xwpbg ~/frames/ --fps 10"
    echo ""
    echo "With ffmpeg:"
    echo "  mkdir -p ~/frames"
    echo "  ffmpeg -i video.mp4 -vf fps=10 ~/frames/frame_%04d.png"
    echo ""
    echo "Static images still work without ffmpeg:"
    echo "  xwpbg image.jpg"
fi

echo ""
echo -e "${YELLOW}Restart your terminal for PATH changes to take effect.${NC}"
