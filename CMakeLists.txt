cmake_minimum_required(VERSION 3.29...3.31)
project(IMP3D VERSION 0.1.5 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable AddressSanitizer for all C++ targets in Debug builds
# if(CMAKE_BUILD_TYPE MATCHES "Debug")
#     set(ASAN_FLAGS "-fsanitize=address -g")
#     set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ASAN_FLAGS}")
#     set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${ASAN_FLAGS}")
#     set(CMAKE_EXE_LINKER_FLAGS   "${CMAKE_EXE_LINKER_FLAGS} ${ASAN_FLAGS}")
#     set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${ASAN_FLAGS}")
# endif()

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Add The static libs that do all the work
add_subdirectory(MeshLib)
add_subdirectory(CoreLib)

# Add_subdirectory(User interface. The mighty IMP3D)
add_subdirectory(ApplicationUI)

# Add other Qt-related settings (e.g., AUTOMOC)
# set_target_properties(ApplicationUI PROPERTIES AUTOMOC ON)
