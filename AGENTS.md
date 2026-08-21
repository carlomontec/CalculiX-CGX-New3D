# CalculiX CGX-New3D Project Architecture & Agreed Design Decisions

This document records the architectural standards, technical decisions, cross-platform requirements, and strict guidelines agreed upon for this project. All AGY / IDE agents must strictly follow these rules.

---

## 1. 🛑 Strict Workflow & Commit Protocol
* **NEVER Commit or Push without Explicit User Permission**: Do not run `git commit` or `git push` unless Carlo explicitly tells you to do so.
* **Always Propose Implementation Plans First**: Present plans using `implementation_plan.md`, explain changes clearly, and wait for approval before editing code.
* **Always Build & Test Locally**: Test changes before reporting completion:
  ```bash
  make -C cgx/CalculiX-CGX-New3D/cgx_2.23/src -f Makefile.glfw -j4 && ./cgx/bin/cgx_glfw test/beam_modal.frd
  ```
* **Language**: All technical documentation, code comments, and chat explanations must remain in English.

---

## 2. 🌍 Cross-Platform Architecture (Mac, Linux, Windows)
* **Development Platform**: macOS (Apple Silicon / Clang / Homebrew GLFW / OpenGL framework).
* **Target Platforms**: macOS, Linux (X11/Wayland with GLFW3), and Windows (MSVC/MinGW with GLFW3).
* **Modernized Windowing / Input**:
  * All legacy X11 / GLX / raw GLUT dependencies are replaced by our clean GLFW3 layer (`cgx_glut_glfw.c` / `cgx_glut_glfw.h`).
  * Keep code strictly portable: avoid platform-specific Cocoa / Win32 / X11 APIs in core CGX code; route windowing, mouse, keyboard, and menus exclusively through GLFW3 and standard OpenGL.
* **Preserve Core Engine & Compatibility**:
  * Respect Klaus Wittig's original hand-crafted CGX engine.
  * Maintain 100% backward compatibility with CGX batch commands, macros (`.fbl`), mesh generators (`libSNL`), file parsers (`.frd`, `.inp`, `.stl`, `.step`), and VTU export.

---

## 3. 🎨 OpenGL State Encapsulation & Rendering Rules
* **Strict State Isolation for HUD / 2D Overlays**:
  * Whenever rendering 2D elements (Color scale bar `scala_tex`, ruler `drawRuler`, command bar, cascading menus), ALWAYS isolate OpenGL state using `glPushAttrib(GL_ALL_ATTRIB_BITS)` and `glPopAttrib()`.
  * Never leak disabled depth-test, custom blending, or projection matrices into the 3D model viewport (prevents hollow/missing faces bugs).
* **Solid Color Scale Bar**:
  * Direct RGB quad rendering with CCW front-facing winding (replacing legacy 1D textures).
* **Stationary 2D Viewport Ruler**:
  * Pinned to bottom-right HUD via isolated orthogonal projection (`glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0)`), completely unaffected by 3D model rotation or zoom.

---

## 4. 🖋️ Typography & Fonts (Key Decisions)
* **NO Custom 1-Bit Bitmaps / NO Iosevka**:
  * Generating custom 1-bit raw bitmap glyphs for classic OpenGL `glBitmap` causes severe endianness, byte-packing, inversion, mirroring, and aliasing issues.
  * We **explicitly bailed** on custom Iosevka bitmaps. Do not re-introduce raw bitmap font generation.
* **NO Times Roman**:
  * Times Roman is completely removed from all C variables, header font tables, 3D annotations, ruler, and UI.
* **Default Font**:
  * Clean, built-in **Helvetica / Arial** and smooth scalable vector typography.
* **3-Tier Text Sizing**:
  * **Default is the Middle size (Medium / Standard ~24px)** so users can scale either up or down.
  * **Small (18px)** (compact & dense)
  * **Medium (24px / Default)** (comfortable modern reading)
  * **Large (34px / Bigger)** (+50% larger than medium for high-DPI screens)

---

## 5. 🖥️ Application Defaults
* **Window Title**: `CalculiX GraphiX (GLFW Edition)`
* **Dark Mode by Default**:
  * `backgrndcol = 0`, `backgrndcol_rgb = [0.05, 0.07, 0.10, 1.0]` (signature `#0D121A` dark slate).
  * `foregrndcol = 1`, `foregrndcol_rgb = [0.92, 0.95, 0.98, 1.0]`.
  * Initial launch background must exactly match the toggle and command line bar color on startup in `initModel()`.
* **Perspective 3D Projection by Default**:
  * `perspectiveFlag = 1` on startup.

---

## 6. 🎛️ UI & Cascading Menu Structure
* **`GUI Settings >` Submenu**:
  * Dedicated top-level submenu in the Main Menu containing:
    * `Toggle Dark Mode`
    * `Toggle Perspective 3D`
    * `Toggle Command Line Bar`
    * `Text Size >` (`Small (18px)`, `Medium (24px / Default)`, `Large (34px)`)
* **No Duplicate Menu Entries**:
  * Do NOT put `Toggle Dark Mode` or `Toggle Perspective 3D` in the `Viewing` menu.
  * `Colormap` lives strictly in `Viewing -> Colormap` (NOT duplicated in Main Menu).
* **Command Line Bar**:
  * No misaligned blinking box cursor (text is displayed cleanly).
  * `Up` arrow key browses previous command history.
  * Sizing and padding adapt dynamically to the active UI text scale.
