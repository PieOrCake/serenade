cmake_minimum_required(VERSION 3.20)
project(Serenade)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release build for optimized, stripped output
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

# Cross-compilation setup for Windows on Linux
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Static linking to avoid Windows DLL dependencies
set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++ -DWIN32_LEAN_AND_MEAN")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui)
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui/backends)

# ImGui source files
set(IMGUI_SOURCES
    lib/imgui/imgui.cpp
    lib/imgui/imgui_draw.cpp
    lib/imgui/imgui_tables.cpp
    lib/imgui/imgui_widgets.cpp
)

# Source files
set(SOURCES
    src/dllmain.cpp
    src/MusicPlayer.cpp
    src/SongParser.cpp
    src/PlaylistEditor.cpp
    ${IMGUI_SOURCES}
)

# Header files
set(HEADERS
    include/nexus/Nexus.h
    src/MusicPlayer.h
    src/SongParser.h
    src/PlaylistEditor.h
)

# Create shared library (DLL)
add_library(Serenade SHARED ${SOURCES} ${HEADERS})

# Force Windows DLL extension
set_target_properties(Serenade PROPERTIES
    SUFFIX ".dll"
    PREFIX ""
)

# Link Windows libraries
target_link_libraries(Serenade
    gdi32
    winmm
)

# Module definition file for clean DLL export
target_link_options(Serenade PRIVATE -Wl,--out-implib,Serenade.lib -Wl,--strip-all)
target_sources(Serenade PRIVATE ${CMAKE_SOURCE_DIR}/Serenade.def)

# Output directory
set_target_properties(Serenade PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
