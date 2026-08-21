# CalculiX GraphiX (GLFW Edition) — Linux Development Context & Roadmap

> **For AGY CLI / IDE agents on Linux**: This document provides the complete technical briefing, architectural decisions, and specific Linux polish checklist to continue modernization work on Linux.

---

## 1. Project Background & Architecture

* **Repository**: [`carlomontec/CalculiX-GraphiX-GLFW`](https://github.com/carlomontec/CalculiX-GraphiX-GLFW)
* **Goal**: Modernize Klaus Wittig's legendary **CalculiX GraphiX (CGX)** engine with pure cross-platform windowing (GLFW3), crisp vector typography (`stb_truetype`), 3D perspective projection, signature dark mode, an in-window command bar, cascading menus, and native VTU/PVD export — while preserving 100% of Klaus's original finite element mechanics and file formats (`libSNL`, `.frd`, `.inp`, `.step`, `.stl`).
* **Zero GLUT / Zero Legacy X11**:
  - Legacy `glut-3.5` and 1-bit bitmap tables are completely eliminated.
  - The windowing layer is encapsulated in `cgx_2.23/src/cgx_glut_glfw.c` and `cgx_glut_glfw.h`.
  - Fonts are dynamically baked into an OpenGL texture atlas using Sean Barrett's single-header [`stb_truetype.h`](cgx_2.23/src/stb_truetype.h).

---

## 2. Linux Dependencies & Build Command

### Prerequisites by Distro:
* **Ubuntu / Debian / Linux Mint**:
  ```bash
  sudo apt-get update && sudo apt-get install -y build-essential libglfw3-dev libgl1-mesa-dev libglu1-mesa-dev fonts-dejavu-core fonts-liberation
  ```
* **Fedora / RHEL / Rocky**:
  ```bash
  sudo dnf install -y gcc-c++ make glfw-devel mesa-libGL-devel mesa-libGLU-devel dejavu-sans-fonts liberation-sans-fonts
  ```
* **Arch Linux / Manjaro**:
  ```bash
  sudo pacman -S --needed base-devel glfw-x11 mesa glu ttf-dejavu ttf-liberation
  ```

### Build Command:
```bash
cd cgx_2.23/src
make -f Makefile.glfw -j$(nproc)
```
Output binary: `../../../bin/cgx_glfw` (or `../../bin/cgx_glfw` depending on current directory).

---

## 3. Key Linux Verification & Polish Checklist

When running and testing on Linux (under X11 and Wayland):

### A. TrueType System Font Discovery
* In `cgx_glut_glfw.c` (`init_truetype_fonts()`), verify that standard Linux TTF fonts load cleanly:
  - `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf`
  - `/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf`
  - `/usr/share/fonts/TTF/DejaVuSans.ttf`
  - `/usr/share/fonts/truetype/freefont/FreeSans.ttf`
* If a distro has fonts in non-standard locations, check `fontconfig` or fallback to any available system sans-serif font.

### B. High-DPI / Framebuffer Scaling (Wayland vs X11)
* `cgx_glut_glfw.c` uses:
  ```c
  glfwGetFramebufferSize(w, &fb_w, &fb_h);
  glfwGetWindowSize(w, &win_w, &win_h);
  float fb_scale = (float)fb_w / (float)win_w;
  ```
* Verify on Linux that font atlases bake sharply on 100% scale displays as well as fractional / high-DPI scaling (125%, 150%, 200%).

### C. Keyboard & Mouse Input on Linux
* **In-Window Command Bar**:
  - Typing commands in the bottom bar (`Enter` executes).
  - `Up` and `Down` arrow keys navigate command history.
  - `Backspace` deletes characters.
* **Cascading Context Menus**:
  - Right-click opens the cascading menu.
  - Hovering over submenus (`Viewing >`, `GUI Settings >`, etc.) cascades smoothly.
  - `Escape` dismisses menus.

### D. 3D Rendering & State Isolation
* Verify that changing colormaps, toggling `GUI Settings > Toggle Dark Mode` or `Toggle Perspective 3D`, and resizing the window do not cause viewport artifacts or projection matrix leaks.
* Verify stationary ruler at bottom-right HUD stays pinned and legible.

### E. Test Models
Test with the included models:
```bash
./cgx/bin/cgx_glfw test/beam_modal.frd
```
Test ParaView export:
```text
send all vtu all
```
Verify generated `.vtu` and `.pvd` files open correctly in ParaView.

---

## 4. AGY Development Rules
* **Never commit or push without explicit user permission**.
* Present clear plans before making large edits.
* All code comments, logs, and docs must be in English.
* Respect Klaus Wittig's engine logic and preserve backward compatibility with all CGX commands and solver formats.
