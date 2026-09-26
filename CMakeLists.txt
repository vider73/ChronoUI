cmake_minimum_required(VERSION 3.21)

# --- Auto-detect vcpkg toolchain ----------------------------------------
# Has to come BEFORE project(): CMake only reads CMAKE_TOOLCHAIN_FILE on the
# first project() call. The framework needs nothing from vcpkg; ClaudeMM does.
if(NOT CMAKE_TOOLCHAIN_FILE)
    foreach(_vcpkg_root "C:/vcpkg" "C:/dev/vcpkg" "$ENV{VCPKG_ROOT}")
        if(EXISTS "${_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "${_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
                CACHE STRING "vcpkg toolchain (auto-detected)" FORCE)
            message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
            break()
        endif()
    endforeach()
endif()

project(ChronoUIProject LANGUAGES CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_custom_target(SolutionItems SOURCES README.md CLAUDE.md docs/EXAMPLES.md docs/WIDGETS.md)
set_target_properties(SolutionItems PROPERTIES FOLDER "Solution Items")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    # /utf-8   the sources are UTF-8 (wide literals with an em dash otherwise decode as garbage)
    # /bigobj  header-only apps pull in many inline templates and pass the COFF section cap
    # /MP      compile the .cpp files within a project in parallel; MSBuild's /m does it across projects
    # /MP is mutually exclusive with /ZI (Edit-and-Continue); we build from the command line
    # and keep plain /Zi.
    add_compile_options(/W4 /permissive- /utf-8 /bigobj /MP /D_CRT_SECURE_NO_WARNINGS)
    add_compile_definitions(UNICODE _UNICODE)
endif()

# ---------------------------------------------------------
# The framework is headers: include/V*.hpp on top of ChronoController.hpp
# (the D2D / DWrite / WIC factories). ChronoUI::Virtual is the target another
# project links to get the include path; there is nothing to build.
#
#     add_subdirectory(path/to/ChronoUI chronoui)
#     target_link_libraries(MyApp PRIVATE ChronoUI::Virtual)
# ---------------------------------------------------------
add_library(chronoui_virtual INTERFACE)
add_library(ChronoUI::Virtual ALIAS chronoui_virtual)
target_include_directories(chronoui_virtual INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(chronoui_virtual INTERFACE cxx_std_17)

option(CHRONOUI_BUILD_EXAMPLES "Build the examples (and the side apps when their folders are present)" ON)
option(CHRONOUI_LEGACY "Build the older DLL model in legacy/: ChronoUI.dll, the cw.* widgets, their demos" ON)

# Every example is a GUI app and wants the Common-Controls v6 manifest so the
# native dialogs pick up the modern theme. legacy/ reads this too.
if(MSVC)
    set(CHRONOUI_MANIFEST
        " /MANIFESTDEPENDENCY:\"type='win32' name='Microsoft.Windows.Common-Controls'"
        " version='6.0.0.0' processorArchitecture='*'"
        " publicKeyToken='6595b64144ccf1df' language='*'\"")
    string(JOIN "" CHRONOUI_MANIFEST ${CHRONOUI_MANIFEST})
endif()

# The assets (the sample image) land next to the executables so the examples
# resolve them as <exe_dir>/assets/<file> whatever the working directory.
# One staging target, not one post-build per exe: N parallel copies of the
# same tree into the same place raced under MSBuild /m.
add_custom_target(chronoui_assets
    COMMAND ${CMAKE_COMMAND}
            -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/assets
            -DDST=${CMAKE_BINARY_DIR}/$<CONFIG>/assets
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageAssets.cmake
    COMMENT "Staging assets next to the executables"
    VERBATIM
)
set_target_properties(chronoui_assets PROPERTIES FOLDER "Examples")

# The DLL model lives in legacy/ and only in this checkout (see legacy/README.md).
if(CHRONOUI_LEGACY AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/legacy/CMakeLists.txt")
    add_subdirectory(legacy)
endif()

if(NOT CHRONOUI_BUILD_EXAMPLES)
    return()
endif()

# ---------------------------------------------------------
# The examples: one source file each, headers only, no DLL of ours. The SDK
# libraries come in through #pragma comment(lib) in the headers.
# VirtualShowcase is the guided tour; the rest each teach one thing
# (docs/EXAMPLES.md). Adding one = a new .cpp and a name in this list.
# ---------------------------------------------------------
set(CHRONOUI_VIRTUAL_EXAMPLES
    VirtualHello VirtualShowcase Dashboard Kanban Controls Form Table Gallery Bounce Settings Mail Photos Catalog Booking)
foreach(_ex ${CHRONOUI_VIRTUAL_EXAMPLES})
    add_executable(${_ex} WIN32 src/examples/${_ex}.cpp)
    target_link_libraries(${_ex} PRIVATE ChronoUI::Virtual)
    set_target_properties(${_ex} PROPERTIES FOLDER "Examples")
    if(MSVC)
        set_property(TARGET ${_ex} PROPERTY WIN32_EXECUTABLE TRUE)
        set_property(TARGET ${_ex} APPEND_STRING PROPERTY LINK_FLAGS "${CHRONOUI_MANIFEST}")
    endif()
endforeach()
foreach(_ex VirtualHello Catalog Booking)     # the ones that load a file from assets/
    add_dependencies(${_ex} chronoui_assets)
endforeach()

# --- The side apps -----------------------------------------------------------
# JLToys (a tray of desktop toys) and ClaudeMM (a Claude Code session
# organiser) live here as proof the framework carries real UI. Their sources
# stay out of the public tree (tools/sync_public.py), so each is built only
# when its folder is present. ClaudeMM also needs nlohmann/json.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JLToys/CMakeLists.txt")
    add_subdirectory(JLToys)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ClaudeMM/CMakeLists.txt")
    find_package(nlohmann_json QUIET)
    if(nlohmann_json_FOUND)
        add_subdirectory(ClaudeMM)
    else()
        message(STATUS "Skipping ClaudeMM (needs nlohmann-json — "
                       "vcpkg install nlohmann-json:x64-windows).")
    endif()
endif()
