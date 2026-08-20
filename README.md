# CalculiX GraphiX (new Engine) 🚀

> **Modern, High-Performance 3D Visualizer & Pre/Post-Processor for the CalculiX FEA Suite.**

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](COPYING)
[![Platform: macOS | Linux](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux-brightgreen.svg)](#-quick-start--build-instructions)
[![Backend: GLFW + OpenGL](https://img.shields.io/badge/Engine-GLFW%203%20%2B%20OpenGL-orange.svg)](#-highlights--new-features)
[![ParaView: Native VTU/PVD](https://img.shields.io/badge/ParaView-Native%20VTU%2FPVD-purple.svg)](#-native-paraview-vtupvd-exporter)

---

## 🌟 What is CalculiX GraphiX (new Engine)?

`CalculiX-CGX-New3D` is a modernized engine for **CalculiX GraphiX (CGX)**, the interactive 3D finite element pre- and post-processor created by **Klaus Wittig** as part of the renowned open-source [CalculiX](http://www.calculix.de/) finite element analysis system.

This new engine replaces legacy 1990s X11 / GLUT dependencies with a high-performance **GLFW & modern OpenGL architecture**, delivering a crisp, fluid, native experience on modern operating systems.

---

## ✨ Highlights & New Features

* 🍏 **Zero X11 / XQuartz Requirement on macOS**: Runs completely native via Cocoa & Metal OpenGL pipelines with full Apple Silicon (M1/M2/M3/M4) and Retina display support.
* 🐧 **Native Linux Wayland & X11 Compatibility**: Fluid hardware-accelerated rendering on modern Linux desktops without legacy X11 lock-in.
* ⌨️ **Interactive In-Window Command Bar**: Sleek bottom command strip with live execution, autocomplete feel, and `Up`/`Down` arrow command history.
* 🪟 **Multi-Level Cascade Popup Menus**: Modern translucent dropdown menus with hover feedback, submenus, and larger click targets.
* 🎥 **True 3D Perspective Projection**: Toggle between realistic 3D perspective depth and classic isometric orthographic views with unbounded close-up zooming.
* 🧭 **Vibrant RGB Coordinate Triad (CSYS)**: Color-coded $X$ (Red), $Y$ (Green), and $Z$ (Blue) axes that smoothly rotate with the model.
* 📊 **Native Base64 Binary VTU/PVD Exporter**: 1-click export of complex 1D, 2D, and 3D meshes and transient results to ParaView.
* 🎨 **Perceptual Scientific Colormaps**: Built-in `coolwarm`, `turbo`, `viridis`, `inferno`, `jet`, `gray`, and `classic` palettes.
* 🌙 **Dark Mode & Light Mode**: Switch instantly between sleek dark charcoal theme (`#080C11`) and classic clean white.
* 🔤 **Crisp Cross-Platform Typography**: Universal Helvetica 18 bitmap font compiled directly from C source tables (zero external font dependencies).

---

## 🚀 Quick Start & Build Instructions

### 🍏 macOS (Homebrew)

1. **Install GLFW**:
   ```bash
   brew install glfw
   ```

2. **Clone & Compile**:
   ```bash
   git clone https://github.com/carlomontec/CalculiX-CGX-New3D.git
   cd CalculiX-CGX-New3D/cgx_2.23/src
   make -f Makefile.glfw -j$(sysctl -n hw.ncpu)
   ```

3. **Launch**:
   ```bash
   ../../bin/cgx_glfw
   ```

---

### 🐧 Linux (Ubuntu, Debian, Fedora, Arch, openSUSE)

1. **Install Prerequisites**:
   * **Ubuntu / Debian / Linux Mint**:
     ```bash
     sudo apt-get update && sudo apt-get install -y build-essential libglfw3-dev libgl1-mesa-dev libglu1-mesa-dev
     ```
   * **Fedora / RHEL / Rocky**:
     ```bash
     sudo dnf install -y gcc-c++ glfw-devel mesa-libGL-devel mesa-libGLU-devel
     ```
   * **Arch Linux / Manjaro**:
     ```bash
     sudo pacman -S --needed base-devel glfw-x11 mesa glu
     ```

2. **Clone & Compile**:
   ```bash
   git clone https://github.com/carlomontec/CalculiX-CGX-New3D.git
   cd CalculiX-CGX-New3D/cgx_2.23/src
   make -f Makefile.glfw -j$(nproc)
   ```

3. **Launch**:
   ```bash
   ../../bin/cgx_glfw
   ```

---

## 🎮 Controls & Mouse Gestures

| Gesture | Action |
| :--- | :--- |
| **Left Click + Drag** | **Rotate 3D Model** (smooth trackball rotation around center) |
| **Right Click + Drag** | **Pan / Translate** model horizontally & vertically |
| **Scroll Wheel** or **Middle Drag** | **Smooth Continuous Zoom** (unbounded zoom range) |
| **Right Click** (click & release) | **Open Multi-Level Popup Menu** |

---

## ⚡ Popular Commands (Type in Bottom Bar)

| Command | Action |
| :--- | :--- |
| `frame` | Auto-fit and center the 3D model in the viewport |
| `view persp` | Switch to realistic 3D Perspective Projection |
| `view ortho` | Switch to classic Orthographic (Isometric) parallel view |
| `view dark` | Switch viewport to modern Dark Mode (`#080C11`) |
| `view light` | Switch viewport to classic Light Mode (White) |
| `ds <step> e <comp>` | Load and display specific result dataset (e.g. `ds 4 e 4`) |
| `plot fv all` | Plot filled contour values on entire model |
| `plot f all` | Plot filled surfaces |
| `plot e all` | Plot element wireframe mesh |
| `cmap <palette>` | Change colormap (`coolwarm`, `turbo`, `viridis`, `inferno`, `jet`, `classic`) |
| `anim real` | Start real-time modal or transient animation |
| `send all vtu all` | Export entire model and all time-steps to ParaView `.vtu` & `.pvd` |

---

## 📦 Native ParaView VTU/PVD Exporter

Export your CalculiX models directly to ParaView with full result fields:

```text
send <set> vtu [all] [ascii]
```

* **Default Base64 Binary Encoding**: Ultra-compact and fast to load.
* **1-Click Time-History Playback**: Running `send all vtu all` creates individual time-step `.vtu` files and automatically links them into a master ParaView Collection file (`.pvd`).
* **Full Element Support**: Converts TET4, TET10, HEX8, HEX20, WEDGE6, WEDGE15, PYR5, PYR13, TR3, TR6, QUAD4, QUAD8, BEAM2, BEAM3 to canonical VTK cells.

---

## 📖 Further Documentation

* 📘 [**New Engine User Guide** (`NEW_ENGINE_GUIDE.md`)](cgx_2.23/NEW_ENGINE_GUIDE.md) — Comprehensive deep-dive on modern features, keyboard shortcuts, and architecture.
* 📚 [**Official LaTeX Manual** (`doc/cgx.tex`)](cgx_2.23/doc/cgx.tex) — Upstream manual with all keywords and technical reference.

---

## ⚖️ License & Attribution

This project is free and open-source software distributed under the **GNU General Public License Version 2 (GPL-2.0 or later)**, strictly adhering to original CalculiX licensing.

### Upstream Authors & Copyright:
* **CalculiX GraphiX (CGX)** is created and copyrighted by **Klaus Wittig** (`klaus.h.wittig@t-online.de`).
* **CalculiX CrunchiX (CCX)** is created and copyrighted by **Guido Dhondt** (`dhondt@t-online.de`).
* Official CalculiX website: [http://www.calculix.de](http://www.calculix.de) / [https://www.dhondt.de](https://www.dhondt.de)

### Project Maintainer & AI Pairing:
* Modernized by **Carlo Monjaraz-Tec** ([@carlomontec](https://github.com/carlomontec)) in collaboration with **Gemini 3.7 Flash** as an open-source exploration of modern scientific visualization architectures.
