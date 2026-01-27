cmake_minimum_required(VERSION 3.23)
project(CoreLib VERSION 0.1 LANGUAGES CXX)

include(FetchContent)

# Fetch GLM
FetchContent_Declare(
  glm
  URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(glm)

if (MSVC)
    target_compile_options(glm INTERFACE /wd4201 /wd4127 /WX-)
endif()

# ------------------------------------------------------------------
# Embree manual wiring (relative to CoreLib/CMakeLists.txt)
# ------------------------------------------------------------------

if (NOT TARGET embree)
    # Embree minimal config
    set(EMBREE_STATIC_LIB ON CACHE BOOL "" FORCE)
    set(EMBREE_ISPC_SUPPORT OFF CACHE BOOL "" FORCE)
    set(EMBREE_TUTORIALS OFF CACHE BOOL "" FORCE)
    set(EMBREE_TESTING OFF CACHE BOOL "" FORCE)
    set(EMBREE_TASKING_SYSTEM INTERNAL CACHE STRING "" FORCE)

    set(EMBREE_GEOMETRY_TRIANGLE ON  CACHE BOOL "" FORCE)
    set(EMBREE_GEOMETRY_QUAD     OFF CACHE BOOL "" FORCE)
    set(EMBREE_GEOMETRY_CURVE    OFF CACHE BOOL "" FORCE)
    set(EMBREE_GEOMETRY_POINT    OFF CACHE BOOL "" FORCE)
    set(EMBREE_GEOMETRY_GRID     OFF CACHE BOOL "" FORCE)
    set(EMBREE_GEOMETRY_SUBDIVISION OFF CACHE BOOL "" FORCE)
    set(EMBREE_GEOMETRY_USER     OFF CACHE BOOL "" FORCE)
    set(EMBREE_GEOMETRY_INSTANCE OFF CACHE BOOL "" FORCE)

    set(EMBREE_ENABLE_STATIC_RUNTIME ON CACHE BOOL "" FORCE) # often helps on Windows
    set(EMBREE_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(EMBREE_RAY_PACKETS OFF CACHE BOOL "" FORCE)          # if available in this version
    set(EMBREE_BACKFACE_CULLING OFF CACHE BOOL "" FORCE)     # not needed for picking usually
    set(EMBREE_SYCL_SUPPORT OFF CACHE BOOL "" FORCE)         # just in case

    # set(EMBREE_MAX_ISA AVX2 CACHE STRING "" FORCE)
    set(EMBREE_MAX_ISA SSE2 CACHE STRING "" FORCE)

    FetchContent_Declare(embree
        GIT_REPOSITORY https://github.com/embree/embree.git
        GIT_TAG        v4.3.1
        GIT_SHALLOW    ON
    )
    FetchContent_MakeAvailable(embree)
    set(EMBREE_INCLUDE_DIR "${embree_SOURCE_DIR}/include")
endif()


# -------------------------------------------------------------------
# END EMBREE
# -------------------------------------------------------------------

# Fetch stb_image
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        master
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(stb)

# Fetch KTX image lib
FetchContent_Declare(
  ktx
  GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
  GIT_TAG        v4.3.2
)

# No tools/tests/docs
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_DOC  OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_VULKAN ON CACHE BOOL "" FORCE) # For glTF KTX2/Basis
set(KTX_FEATURE_STATIC_LIBRARY ON CACHE BOOL "" FORCE) # Static lib
FetchContent_MakeAvailable(ktx)


# Fetch TinyGLTF (header-only)
FetchContent_Declare(
  tinygltf
  GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
  GIT_TAG        v2.8.21   # or a known good tag
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(tinygltf)

# Fetch OpenSubdiv (CPU evaluator only)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TUTORIALS ON CACHE BOOL "" FORCE)
set(NO_REGRESSION ON CACHE BOOL "" FORCE)
set(NO_DOC ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(NO_PTEX ON CACHE BOOL "" FORCE)
set(NO_OPENCL ON CACHE BOOL "" FORCE)
set(NO_CUDA ON CACHE BOOL "" FORCE)
set(NO_DX ON CACHE BOOL "" FORCE)
set(NO_METAL ON CACHE BOOL "" FORCE)
set(NO_OMP ON CACHE BOOL "" FORCE)
set(NO_TBB ON CACHE BOOL "" FORCE)
set(OSD_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(OSD_INSTALL OFF CACHE BOOL "" FORCE)
set(OPENSUBDIV_GL_ENABLED OFF)

# Explicitly enable CPU backend
set(OSD_CPU ON CACHE BOOL "" FORCE)

if (MSVC)
  # Makes MSVC expose M_PI in <cmath>/<math.h>
  add_compile_definitions(_USE_MATH_DEFINES)
endif()

FetchContent_Declare(
  OpenSubdiv
  GIT_REPOSITORY https://github.com/PixarAnimationStudios/OpenSubdiv.git
  GIT_TAG        v3_6_0
)
FetchContent_MakeAvailable(opensubdiv)

# OpenMP
find_package(OpenMP REQUIRED)


# Gather CoreLib sources
file(GLOB_RECURSE CORELIB_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/**/*.hpp")
file(GLOB_RECURSE CORELIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/**/*.cpp")

# Shader directories
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Render/Shaders)
set(SHADER_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/Render/Shaders)
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

# List Vulkan shaders to compile
set(SHADERS
    Grid.vert
    Grid.frag
    Line.vert
    Line.frag
    SolidDraw.vert
    SolidDraw.frag
    ShadedDraw.vert
    ShadedDraw.frag
    Overlay.vert
    Overlay.geom
    Overlay.frag
    Selection.vert
    Selection.frag
    SelectionVert.frag
    Wireframe.vert
    Wireframe.frag
    WireframeDepthBias.vert
    FullscreenTri.vert
    RtPresent.vert
    RtPresent.frag
    RtScene.rgen
    RtScene.rmiss
    RtScene.rchit
)

# Compile shaders to SPIR-V using glslangValidator
foreach(SHADER IN LISTS SHADERS)
    set(SHADER_SRC ${SHADER_SRC_DIR}/${SHADER})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SHADER_SPV ${SHADER_BIN_DIR}/${SHADER_NAME}.spv)

    add_custom_command(
        OUTPUT ${SHADER_SPV}
        COMMAND glslangValidator -V --target-env vulkan1.2 ${SHADER_SRC} -o ${SHADER_SPV}
        DEPENDS ${SHADER_SRC}
        COMMENT "Compiling ${SHADER} to SPIR-V"
        VERBATIM
    )

    list(APPEND SHADER_SPV_BINARIES ${SHADER_SPV})
endforeach()

add_custom_target(compile_shaders ALL DEPENDS ${SHADER_SPV_BINARIES})

# Expose shader sources to CoreLib
file(GLOB_RECURSE SHADER_SOURCES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    Render/Shaders/*.vert
    Render/Shaders/*.frag
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SHADER_SOURCES})

# Define CoreLib target
add_library(CoreLib STATIC
    ${CORELIB_HEADERS}
    ${CORELIB_SOURCES}
    ${SHADER_SOURCES}
)

target_compile_features(CoreLib PUBLIC cxx_std_23)

# Include directories
target_include_directories(CoreLib PUBLIC
    ${glm_SOURCE_DIR}
    ${glad_SOURCE_DIR}/include
    ${stb_SOURCE_DIR}
    ${ktx_SOURCE_DIR}/include
    ${ktx_BINARY_DIR}/include  # sometimes needed for generated headers
    ${tinygltf_SOURCE_DIR}
    ${opensubdiv_SOURCE_DIR}
    ${EMBREE_INCLUDE_DIR}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core/include/VK>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Scene>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Scene/Query>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Scene/Materials>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Scene/SceneOps>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Scene/Objects>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Render>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Render/Helpers>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Render/GpuResources>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Render/Subdivision>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Utilities>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/CoreModules/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Modifiers>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Modifiers/Tools>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Modifiers/Commands>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Modifiers/Ops>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Overlays>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Overlays/ToolSizers>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SceneIO>
)

# Define shader path macro for runtime loading
target_compile_definitions(CoreLib PUBLIC
    # SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Render/Shaders"
    SHADER_BIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/Render/Shaders"
)

# Find Vulkan
find_package(Vulkan REQUIRED)

# Link dependencies
target_link_libraries(CoreLib
    PUBLIC
        MeshLib
        osd_static_cpu
        embree
        ktx
        Vulkan::Vulkan
        "${TBB_LIBRARY}")

if (MSVC)
    target_compile_options(CoreLib PRIVATE /wd4201)  # disable warning C4201
endif()

