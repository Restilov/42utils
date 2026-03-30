# xwpbg

Animated and static wallpaper setter for X11 (KDE, XFWM, Openbox, etc.)

## Installation

```bash
git clone <repo>
cd 42utils/wallpaper
bash install.sh
```

The script installs required dependencies, compiles the binary and adds it to your PATH.

**Supported distros:** Arch Linux, Debian, Ubuntu

**Dependencies (installed automatically):** `gcc`, `libx11`, `libxext`, `libjpeg`, `libpng`, `ffmpeg`

## Usage

### Static wallpaper

```bash
xwpbg image.jpg
xwpbg image.png --foreground
```

### Animated wallpaper from video

```bash
xwpbg video.mp4
xwpbg video.mp4 --fps 24
```

Supported formats: `mp4`, `mkv`, `avi`, `webm`, `mov`, `gif`

Frames are extracted automatically via ffmpeg into a temporary directory and cleaned up on exit.

### Animated wallpaper from image folder

```bash
xwpbg ~/frames/
xwpbg ~/frames/ --fps 15
```

Images inside the folder must be named in alphabetical/numerical order (e.g. `frame_0001.png`).

To extract frames manually:

```bash
mkdir -p ~/frames
ffmpeg -i video.mp4 -vf fps=10 ~/frames/frame_%04d.png
```

### Options

| Option | Description |
|---|---|
| `--fps N` | Frame rate for animated mode (default: 10, max: 60) |
| `--foreground` | Run in foreground instead of daemonizing |

### Stopping

```bash
pkill xwpbg
```
