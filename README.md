# IMP-3D

IMP-3D is a C++ 3D modeling application under active development.  
It focuses on a robust polygonal mesh core, explicit rendering architecture,
and a Vulkan-based rendering backend with optional hardware ray tracing.

The project is structured as a traditional DCC-style application, with
clear separation between geometry, rendering, and user interface layers.

> ⚠️ Early development — APIs, file formats, and behavior may change.

**Project page / downloads:**  
https://dgiannop.github.io/IMP3D-web/

---

## Overview

IMP-3D is built around an authoritative CPU-side mesh representation used
for all topology edits, selection, and undo/redo. Rendering is layered on
top of this core and supports both rasterization and ray tracing through
Vulkan.

The goal of the project is to explore and develop a clean, scalable
architecture for polygonal modeling and modern GPU rendering, with an
emphasis on correctness, debuggability, and long-term maintainability.

---

## Features

- Polygonal mesh editing (vertices, edges, polygons)
- CPU-side mesh core with:
  - Stable element indices
  - Full undo/redo support
  - Face-varying normals and UVs
- Scene-based architecture supporting multiple meshes
- Multiple viewports
- Vulkan renderer with:
  - Solid and wireframe rendering
  - Selection overlays
  - Hardware ray tracing via Vulkan RT extensions
- Subdivision-aware rendering
- Grid snapping and precision modeling tools
- Qt-based desktop user interface

---

## Architecture Notes

- **CPU-first design**  
  All mesh operations are performed on the CPU using a custom mesh data
  structure. GPU resources are treated as derived data and rebuilt only
  when necessary.

- **Face-varying attribute model**  
  Normals and UVs are stored per polygon corner, enabling correct hard
  edges, smooth shading, and subdivision workflows similar to Blender.

- **Explicit change tracking**  
  Lightweight counters are used to track topology, deformation, selection,
  and content changes across the system, avoiding complex dependency graphs.

- **Vulkan-based rendering**  
  Rendering is implemented directly on Vulkan, with explicit resource
  ownership and lifetime management. Rasterization and ray tracing share
  the same scene and mesh infrastructure.

---

## Screenshots

*(Not yet available)*

---

## Build Requirements

- Windows 10 / 11
- CMake 3.26 or newer
- Visual Studio 2022 (MSVC toolchain)
- Vulkan SDK 1.3+
- Qt 6.8+

---

## Building

```bash
git clone https://github.com/dgiannop/IMP-3D.git
cd IMP-3D
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
