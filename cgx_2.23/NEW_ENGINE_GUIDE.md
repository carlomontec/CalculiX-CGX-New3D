# CalculiX GraphiX (new Engine) — Modern 3D Visualizer & Pre/Post-Processor

**CalculiX GraphiX (new Engine)** (`cgx_glfw`) is a modern, cross-platform interactive OpenGL 3D visualizer and pre/post-processor for the CalculiX FEA suite. It replaces legacy X11 / GLUT 3.5 dependencies with high-performance native **GLFW** and pure OpenGL, providing:

* **Zero X11 / XQuartz Requirement on macOS**: Runs completely native via Cocoa & Metal OpenGL pipelines.
* **Native Linux Wayland & X11 Compatibility**: Seamlessly runs on modern Linux desktops.
* **Modern In-Window Command Bar**: Interactive bottom command bar with live execution, history navigation (`Up`/`Down`), and auto-focus.
* **Multi-Level Cascade Glass Popup Menus**: High-definition, multi-level dropdowns with hover feedback and submenus.
* **True 3D Perspective & Orthographic Projections**: Smooth depth projection with unbounded close-up zoom.
* **Vibrant RGB Coordinate Triad (CSYS)**: Bold color-coded coordinate axes ($X$ Red, $Y$ Green, $Z$ Blue).
* **Modern Colormaps**: Built-in perceptual palettes (`coolwarm`, `turbo`, `viridis`, `inferno`, `jet`, `gray`, `classic`).
* **Dark Mode & Light Mode**: Match your workflow between high-contrast dark theme (`#080C11`) and classic clean white.
* **Crisp Cross-Platform Typography**: Embedded Helvetica 18 typography with zero external system font dependencies.

---

## 🛠️ Compilation & Installation

### 1. macOS (Native Apple Silicon / Intel)
Ensure Homebrew and GLFW are installed:
```bash
brew install glfw
```

Build `cgx_glfw`:
```bash
cd cgx_2.23/src
make -f Makefile.glfw -j$(sysctl -n hw.ncpu)
```
The compiled binary will be placed at `cgx/bin/cgx_glfw`.

---

### 2. Linux (Ubuntu, Debian, Fedora, Arch, openSUSE)
Install GLFW development libraries:

* **Debian / Ubuntu / Linux Mint**:
  ```bash
  sudo apt-get install libglfw3-dev libgl1-mesa-dev libglu1-mesa-dev
  ```
* **Fedora / RHEL / Rocky Linux**:
  ```bash
  sudo dnf install glfw-devel mesa-libGL-devel mesa-libGLU-devel
  ```
* **Arch Linux / Manjaro**:
  ```bash
  sudo pacman -S glfw-x11 mesa glu
  ```

Build `cgx_glfw`:
```bash
cd cgx_2.23/src
make -f Makefile.glfw -j$(nproc)
```

---

### 3. Windows (MSYS2 / MinGW-w64)
Install GLFW & GCC in the MSYS2 UCRT64/MINGW64 environment:
```bash
pacman -S --needed base-devel mingw-w64-x86_64-gcc mingw-w64-x86_64-glfw
```

Build `cgx_glfw.exe`:
```bash
cd cgx_2.23/src
make -f Makefile.glfw -j$(nproc)
```
The compiled binary will be placed at `cgx/bin/cgx_glfw.exe`.

---

## 🎮 Controls & Shortcuts Reference

### Mouse Controls

| Action | Control | Description |
| :--- | :--- | :--- |
| **Rotate 3D Model** | **Left Click + Drag** | Smooth trackball 3D rotation around model center |
| **Pan / Translate** | **Right Click + Drag** | Move model horizontally and vertically |
| **Zoom In / Out** | **Scroll Wheel** or **Middle Click + Drag** | Smooth continuous zooming (unbounded range) |
| **Open Menu** | **Right Click** (click & release) | Opens the Multi-Level Cascade Popup Menu |

---

### In-Window Command Bar

| Key | Action |
| :--- | :--- |
| **`Enter` / `Return`** | Executes the command in the 3D viewport |
| **`Up Arrow`** | Recalls previous command from history |
| **`Down Arrow`** | Moves forward through command history |
| **`Escape`** | Clears the current input text or dismisses popup menus |

---

## ⚡ Popular Interactive Commands

Type these directly into the bottom Command Bar:

### Viewing & Projections
* `view persp` — Enable realistic 3D Perspective Projection.
* `view ortho` — Return to classic Orthographic (Parallel) View.
* `frame` — Auto-center and fit model to viewport.
* `view dark` — Switch to modern Dark Charcoal theme (`#080C11`).
* `view light` — Switch to classic Light Mode (White background).
* `view edge` — Toggle model edges on/off.
* `view elem` — Toggle mesh element outline edges.

### Results & Datasets
* `ds <step> e <entity>` — Display Dataset step and component (e.g. `ds 4 e 4` for total displacement magnitude).
* `plot fv all` — Plot filled contour values on entire model.
* `plot f all` — Plot filled surface faces.
* `plot e all` — Plot mesh element wireframe.
* `cmap coolwarm` — Set colormap to smooth divergent Coolwarm.
* `cmap turbo` — Set colormap to high-detail Turbo rainbow.
* `cmap viridis` — Set colormap to colorblind-safe Viridis.
* `cmap inferno` — Set colormap to high-intensity Inferno.
* `cmap classic` — Set colormap to classic CalculiX colormap.

### Animations
* `anim real` — Play real-time modal/transient animation.
* `anim tune <val>` — Adjust deformation scale factor.
* `plus` / `minus` — Step to next / previous dataset.

---

## 📂 Architecture & Files

* **`cgx_glut_glfw.c` / `cgx_glut_glfw.h`**: Modern GLFW backend implementing windowing, hierarchical coordinate mapping, high-dpi viewports, popup cascade menus, and the interactive command line bar.
* **`Makefile.glfw`**: Cross-platform Makefile auto-detecting Darwin (macOS Cocoa/Metal) vs Linux (Wayland/X11 Mesa GL).
* **`readStdCmap.c`**: Modern perceptual colormap generators (`coolwarm`, `turbo`, `viridis`, `inferno`, `jet`, `gray`).
