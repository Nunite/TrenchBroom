% TrenchBroom  \_\_TB_VERSION\_\_ Reference Manual
% Kristian Duske
% 11-13-2015

# Introduction {#introduction}

TrenchBroom is a modern, cross-platform level editor for brush-based game engines such as Quake, Quake 2, Hexen 2, and GoldSrc (Half-Life). It provides an intuitive workflow, high-performance rendering, and comprehensive modeling and extension tools.

::: manual-hero-banner
**🚀 Welcome to TrenchBroom Documentation**

Explore the complete user guide, modeling tools, material and asset browsers, Python scripting, and editor automation.

::: manual-quick-tags
[⚡ Getting Started](#getting_started){.manual-tag}
[📐 Brush Modeling](#brush_editing_and_creation){.manual-tag}
[✂️ CSG & Vertex Editing](#vertex_and_csg){.manual-tag}
[🎨 Materials & UV](#materials_and_uv){.manual-tag}
[📦 Asset Browser](#assets_and_prefabs){.manual-tag}
[🐍 Python API Plugins](#python_scripting_and_plugins){.manual-tag}
[🤖 MCP Automation](#mcp_automation){.manual-tag}
:::
:::

::: manual-card-grid
::: manual-card
**🚀 1. Getting Started**

Fundamental concepts, map hierarchy (World, Layers, Brushes, Entities), and 3D/2D viewport navigation.

[➔ 3 Chapters](#getting_started)
:::

::: manual-card
**🧱 2. Level Modeling & CSG**

Brush primitives, extrusion, clipping, sweeping, vertex editing, and CSG boolean operations.

[➔ 2 Chapters](#brush_editing_and_creation)
:::

::: manual-card
**🎨 3. Materials, Assets & Scene**

Material browser, UV editor, unified GoldSrc asset browser (MDL, SPR, WAV), prefabs, and Outliner.

[➔ 3 Chapters](#materials_and_uv)
:::

::: manual-card
**⚙️ 4. Pipeline & Extensions**

Preferences, compiler toolchains, engine testing, Python API plugins, and MCP automation.

[➔ 4 Chapters](#preferences_and_compilation)
:::
:::

## Features {#features}

* **General**
  - Full support for editing in 3D and in up to three 2D views
  - High performance renderer with support for huge maps
  - Unlimited Undo and Redo
  - Searchable Command Palette and configurable Pie Menu
  - System, Light, Dark, and Blender themes, plus distributable user themes
  - Macro-like command repetition
  - Linked groups
  - Searchable Outliner with layer creation and inline entity properties
  - Issue browser with automatic quick fixes
  - Run external compilers and launch game engines
  - Interactive Python Console and manifest-based Python API plugins
  - Optional localhost MCP endpoint for guarded editor automation
  - Point file and portal file support
  - Automatic backups
  - Free and cross platform
* **Brush Editing**
  - Robust vertex editing with edge and face splitting and manipulating multiple vertices together
  - Clipping tool with two and three points
  - Scale and shear tools
  - CSG operations: merge, subtract, hollow, intersect
  - UV view for easy texturing manipulations
  - Precise alignment lock for all brush editing operations
  - Multiple material collections
  - Searchable material browser with collapsible collection groups and 100%-500% thumbnails
* **Entity Editing**
  - Entity browser with drag and drop support
  - Unified asset browser for GoldSrc models, sprites, sounds, and reusable prefabs
  - Support for FGD, ENT and DEF files for entity definitions
  - Mod support
  - Entity link visualization
  - Displays 3D models in the editor (supports mdl, md2, md3, bsp, dkm)
  - Smart entity property editors
  - Path Tool for creating linked `path_corner` chains

## About this Document {#about-this-document}

This document is intended to help you learn to use the TrenchBroom editor. It is not intended to teach you how to map, and it is not a tutorial. If you are having technical problems with your maps or need information about creating particular effects or setups for the game you are mapping for, ask other mappers for help (see [References and Links](#references_and_links) to find mapping communities). This document only teaches you how to use the editor.
