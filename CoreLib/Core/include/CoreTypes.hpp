//
//  CoreTypes.hpp
//  Core
//
//  Created by Daniel Giannopoulos on 5/19/16.
//  Copyright © 2016 Daniel Giannopoulos. All rights reserved.
//
// Public enums and types that
// are visible to both C++ and the UI Application

#pragma once

enum class SelectionMode
{
    VERTS,
    EDGES,
    POLYS,
};

enum class ViewMode
{
    PERSPECTIVE,
    TOP,
    BOTTOM,
    FRONT,
    BACK,
    LEFT,
    RIGHT
};

enum class DrawMode
{
    WIREFRAME,
    SOLID,
    SHADED,
    RAY_TRACE,
};

struct CoreEvent
{
    int   button;
    float x;
    float y;
    float deltaX;
    float deltaY;
    int   key_code;
    bool  shift_key;
    bool  ctrl_key;
    bool  cmd_key;
    bool  alt_key;
    bool  dbl_click;
};

enum class PropertyType
{
    INT,
    UINT,
    FLOAT,
    BOOL,
    AXIS,
    COLOR,
    INT_RO, // Read only
    VEC3,
    VEC4
};

struct SceneStats
{
    unsigned int verts = 0;
    unsigned int polys = 0;
    unsigned int norms = 0;
    unsigned int uvPos = 0;
};

enum class GpuBackend
{
    OpenGL, // Not implemented anymore
    Vulkan
};
