cmake_minimum_required(VERSION 3.29...3.31)
project(MeshLib VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  glm
  URL https://github.com/g-truc/glm/archive/refs/tags/0.9.9.8.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)
FetchContent_MakeAvailable(glm)

add_library(MeshLib STATIC
  include/SysCounter.hpp
  include/SysCounter.hpp
  include/SysCounter.hpp
  src/SysCounter.cpp
  include/EdgeSet.hpp
  include/History.hpp
  src/History.cpp
  include/HoleList.hpp
  include/SmallList.hpp

  include/SysMesh.hpp
  src/SysMesh.cpp

  include/SysMeshUtils.hpp
  include/SysObjLoader.hpp
  src/SysObjLoader.cpp
  include/SysMeshScene.hpp
  src/SysMeshScene.cpp

  include/HeMesh.hpp
  src/HeMesh.cpp

  include/HalfEdgeView.hpp
  src/HalfEdgeView.cpp
  src/SysMeshData.hpp
  src/SysHistoryActions.hpp
  include/HeMeshBridge.hpp
  src/HeMeshBridge.cpp
  src/SysMeshUtils.cpp
  src/SysMeshUtils.cpp


)

target_include_directories(MeshLib PUBLIC
  ${glm_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/Mesh)

if(MSVC)
    target_compile_options(MeshLib PRIVATE
        /W4        # Enable high level of warnings
        /WX        # Treat warnings as errors
    )
else()
    target_compile_options(MeshLib PRIVATE
        -Wall      # Enable all the commonly used warning messages
        -Wextra    # Enable some extra warning messages that are not covered by -Wall
        -Wpedantic # Enable strict ISO C and ISO C++ compliance
    )
endif()

if (MSVC)
    target_compile_options(MeshLib PRIVATE /wd4201)  # disable warning C4201
endif()
