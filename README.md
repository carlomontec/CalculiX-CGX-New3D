# CalculiX-CGX-New3D (Proof of Concept)

> **Experimental modern graphics and exporter exploration for CalculiX GraphiX (CGX).**

---

## 🌟 About & Philosophy

`CalculiX-CGX-New3D` is an experimental, community-driven proof-of-concept (PoC) fork of **CalculiX GraphiX (CGX)**, the interactive 3D finite element pre- and post-processor created by **Klaus Wittig** as part of the [CalculiX](http://www.calculix.de/) suite.

The goal of this project is to explore modernizing the 3D visualization and interoperability pipelines for the broader CalculiX community while preserving complete backward compatibility with standard CGX workflows.

---

## 🎯 Key Goals & Roadmap

1. **Modern Cross-Platform Windowing Engine (GLFW)**:
   - **Linux**: Native **Wayland** and modern X11 support (no legacy X11 library dependencies).
   - **macOS**: Native **Cocoa** windowing (completely eliminating the need for XQuartz / X11).
   - **High-DPI**: Crisp 4K and Retina display resolution scaling.
2. **Native VTK / ParaView Exporter (`.vtu`)**:
   - Direct export of 1D, 2D, and 3D meshes with nodal/element result fields (displacements, stresses, temperatures) to VTK XML Unstructured Grids for seamless post-processing in ParaView.
3. **Blender & Modern 3D Bridge (`.gltf` / `.ply`)**:
   - Export deformed FE surface geometry with field-baked vertex colors for photorealistic rendering, animations, and interactive web 3D.

---

## ⚖️ License & Attribution

This project is free and open-source software distributed under the **GNU General Public License Version 2 (GPL-2.0 or later)**, strictly adhering to the original CalculiX GraphiX licensing.

### Upstream Authors & Copyright:
* **CalculiX GraphiX (CGX)** is created and copyrighted by **Klaus Wittig** (`klaus.h.wittig@t-online.de`).
* **CalculiX CrunchiX (CCX)** is created and copyrighted by **Guido Dhondt** (`dhondt@t-online.de`).
* Official CalculiX website and upstream releases: [http://www.calculix.de](http://www.calculix.de) / [https://www.dhondt.de](https://www.dhondt.de)

### Third-Party Components & Permissive Notices:
* **libSNL**: Non-Uniform B-Spline library (GNU GPL v2).
* **Trackball**: Silicon Graphics, Inc. (Permissive SGI copyright notice preserved).
* **GLUT**: Developed by Mark J. Kilgard (Permissive freely distributable notice preserved).

*All modifications and additions in this fork are likewise published under GPL-2.0+.*

---

## 🤖 Development Note

This experimental fork is developed by **Carlo Monjaraz-Tec** ([@carlomontec](https://github.com/carlomontec)) in collaboration with **Gemini 3.7 Flash** as an AI pair-programming exploration of modern scientific visualization architectures.

---

## 🚀 Building & Getting Started

### Prerequisites (macOS via Homebrew)
```bash
brew install gcc mesa-glu libxi libxmu pkg-config
```

### Build
```bash
cd cgx_2.23/src
make -j4
```

### Run
```bash
./cgx
```

---

## 💬 Feedback & Disclaimer

*This is an independent proof-of-concept project intended for research, experimentation, and sharing ideas with the CalculiX community.*
