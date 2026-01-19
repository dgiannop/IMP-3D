# IMP-3D

IMP-3D is a modern, high-performance 3D modeling application focused on
robust mesh editing, real-time rendering, and Vulkan-based ray tracing.

> ⚠️ Early development — APIs, file formats, and features are still evolving.

---

## Features

- Polygonal modeling (verts / edges / faces)
- Robust mesh core with stable indices and undo/redo
- Vulkan renderer with multiple viewports
- Hardware ray tracing (DXR-style pipeline via Vulkan)
- Face-varying normals and UVs
- Qt-based UI

---

## Screenshots

*(Coming soon)*

---

## Build Requirements

- Windows 10 / 11
- CMake 3.26+
- Visual Studio 2022 (MSVC)
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
