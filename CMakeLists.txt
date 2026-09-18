cmake_minimum_required(VERSION 3.21)   # 3.21+ por $<TARGET_RUNTIME_DLLS>

# --- Auto-detect vcpkg toolchain ----------------------------------------
# Tiene que estar ANTES de project() — CMake sólo procesa
# CMAKE_TOOLCHAIN_FILE en la primera llamada a project().
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

# ---------------------------------------------------------
# CRITICAL FIX 1: Enable Solution Folders
# ---------------------------------------------------------
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Force C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Solution items (shown at the root of the VS solution) ---
set(SOLUTION_FILES
    assets/bootstrap_lite.css
    README.md
)
add_custom_target(SolutionItems SOURCES ${SOLUTION_FILES})
set_target_properties(SolutionItems PROPERTIES FOLDER "Solution Items")

# Global Include Directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/widgets)

# MSVC Specific Flags
if(MSVC)
    # /utf-8 — tell the compiler our source files are UTF-8 (otherwise wide
    # string literals like L"em-dash —" are mis-decoded as the system code page
    # and show up as garbage characters at runtime).
    # /bigobj — raises the COFF section cap to 2^32. Header-only apps that pull
    # in a lot of inline templates blow past the 65535 limit (C1128); the only
    # cost is slightly larger .obj files.
    # /MP — compile the .cpp files WITHIN each project in parallel (one cl.exe per core).
    # MSBuild's /m (in doNextStage.ps1) already parallelizes ACROSS projects; /MP adds the
    # missing within-project parallelism, so a big machine actually gets used. The two
    # together = full parallel build (the single biggest speed win here).
    add_compile_options(/W4 /permissive- /utf-8 /bigobj /MP /D_CRT_SECURE_NO_WARNINGS)
    add_compile_definitions(UNICODE _UNICODE)

    # NOTE: /MP is mutually exclusive with /ZI (Edit-and-Continue). We build from the
    # command line (doNextStage.ps1), where E&C does nothing, so we keep plain /Zi (full
    # debug info, breakpoints, call stacks all work) and take the parallel build instead.
    # If you ever need IDE Edit-and-Continue, drop /MP and restore /ZI + /EDITANDCONTINUE.
endif()

# ---------------------------------------------------------
# What to build. The framework itself is always built; the widgets, the
# examples and the side apps are optional so another project can pull
# ChronoUI in with add_subdirectory() and get just the library.
# ---------------------------------------------------------
option(CHRONOUI_BUILD_EXAMPLES
       "Build the cw.* widget DLLs, the examples and the side apps" ON)

# ---------------------------------------------------------
# 1. Define the Core Library (ChronoUI)
# ---------------------------------------------------------
add_library(ChronoUI SHARED 
    include/funMessageBox.hpp
    include/ChronoUI.hpp
    include/WidgetImpl.hpp
    include/ContextNodeImpl.hpp
    include/ChronoStyles.hpp
    src/core/ChronoUI.cpp
    src/core/ChronoStyles.cpp
)
target_compile_definitions(ChronoUI PRIVATE CHRONOUI_EXPORTS)
target_link_libraries(ChronoUI PRIVATE user32 gdi32 dwmapi)

set_target_properties(ChronoUI PROPERTIES
    FOLDER "Core"
)

if(NOT CHRONOUI_BUILD_EXAMPLES)
    return()   # a parent project just wants ChronoUI.dll
endif()

# ---------------------------------------------------------
# 2. Define Widget DLLs
# ---------------------------------------------------------

set(WIDGET_SOURCES
    "src/widgets/cw.AnalogClock.cpp"
    "src/widgets/cw.AnimatedParticlesProgress.cpp"
    "src/widgets/cw.Button.cpp"
    "src/widgets/cw.DataPlotControl.cpp"
    "src/widgets/cw.EditBox.cpp"
    "src/widgets/cw.EqualizerBar.cpp"
    "src/widgets/cw.EyesControl.cpp"
    "src/widgets/cw.GaugeBatteryLevelControl.cpp"
    "src/widgets/cw.GaugeEngineTemperatureControl.cpp"
    "src/widgets/cw.GaugeSpeedOmeter.cpp"
    "src/widgets/cw.ImageViewerWidget.cpp"
    "src/widgets/cw.LightingStormOverlay.cpp"
    "src/widgets/cw.ListCards.cpp"
    "src/widgets/cw.Progress.cpp"
    "src/widgets/cw.SliderControl.cpp"
    "src/widgets/cw.SnowingOverlay.cpp"
    "src/widgets/cw.StaticText.cpp"
    "src/widgets/cw.SwitchButton.cpp"
    "src/widgets/cw.TextSlider.cpp"
    "src/widgets/cw.TitleDescCard.cpp"
    "src/widgets/cw.ViewDateTimeWidget.cpp"
    "src/widgets/cw.VitalsMonitor.cpp"
    "src/widgets/cw.WaitingOverlay.cpp"
)

# Initialize a list to keep track of all generated widget targets
set(ALL_WIDGET_TARGETS "")

foreach(WIDGET_PATH ${WIDGET_SOURCES})
    get_filename_component(WIDGET_NAME ${WIDGET_PATH} NAME_WLE)
    
    add_library(${WIDGET_NAME} SHARED "${WIDGET_PATH}")
    
    # Store the target name for the Executables to use later
    list(APPEND ALL_WIDGET_TARGETS ${WIDGET_NAME})

    set_target_properties(${WIDGET_NAME} PROPERTIES 
        WINDOWS_EXPORT_ALL_SYMBOLS TRUE
        PREFIX "" 
        SUFFIX ".dll"
        FOLDER "Widgets"
    )

    # REQ: All widgets depend of chronoui
    # This was already correct in your script.
    target_link_libraries(${WIDGET_NAME} PRIVATE ChronoUI user32 gdi32 dwmapi)
    
    string(TOUPPER ${WIDGET_NAME} WIDGET_UPPER)
    string(REPLACE "." "_" WIDGET_UPPER ${WIDGET_UPPER})
    target_compile_definitions(${WIDGET_NAME} PRIVATE ${WIDGET_UPPER}_EXPORTS)
endforeach()

# ---------------------------------------------------------
# 3. Define the Demo Executables
# ---------------------------------------------------------
add_executable(ChronoUIDemo WIN32 
                src/examples/main.cpp 
                include/virtual_drive.hpp
) 
# WidgetTesterDemo needs nlohmann/json (vcpkg install nlohmann-json). When the
# package is missing we skip that one target instead of failing the configure:
# the framework itself has no external dependencies and must always build.
find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
    add_executable(WidgetTesterDemo WIN32
                    src/examples/WidgetTesterDemo.cpp
                    include/virtual_drive.hpp
    )
    target_link_libraries(WidgetTesterDemo PRIVATE nlohmann_json::nlohmann_json)
else()
    message(STATUS "Skipping WidgetTesterDemo (needs nlohmann-json — "
                   "vcpkg install nlohmann-json:x64-windows).")
endif()
add_executable(LayoutTester WIN32 
                src/examples/LayoutTester.cpp 
                include/virtual_drive.hpp
) 
add_executable(HelloWorld WIN32
                src/examples/HelloWorld.cpp
                include/virtual_drive.hpp
)
add_executable(Welcome WIN32
                src/examples/Welcome.cpp
)
# Virtual-widget examples: one source file each, no assets, and NO DLL of ours:
# the virtual model is header-only (include/V*.hpp), the SDK libraries come in
# through #pragma comment(lib) in the headers. VirtualShowcase is the guided
# tour; the rest each teach one thing (see docs/EXAMPLES.md). Adding one = a
# new .cpp here and a line in that list.
set(CHRONOUI_VIRTUAL_EXAMPLES
    VirtualHello VirtualShowcase Dashboard Kanban Controls Form Table Gallery Bounce Settings Mail Photos Catalog)
foreach(_ex ${CHRONOUI_VIRTUAL_EXAMPLES})
    add_executable(${_ex} WIN32 src/examples/${_ex}.cpp)
    set_target_properties(${_ex} PROPERTIES FOLDER "Examples")
endforeach()

# Every example links the framework; the ones built on the cw.* model also link
# the widget import libs, which additionally makes CMake build the widget DLLs
# before the exe that loads them.
target_link_libraries(ChronoUIDemo PRIVATE ChronoUI ${ALL_WIDGET_TARGETS})
if(TARGET WidgetTesterDemo)
    target_link_libraries(WidgetTesterDemo PRIVATE ChronoUI ${ALL_WIDGET_TARGETS})
endif()
target_link_libraries(LayoutTester   PRIVATE ChronoUI ${ALL_WIDGET_TARGETS})
target_link_libraries(HelloWorld     PRIVATE ChronoUI ${ALL_WIDGET_TARGETS})
target_link_libraries(Welcome        PRIVATE ChronoUI ${ALL_WIDGET_TARGETS})

# --- The side apps -----------------------------------------------------------
# JLToys (a tray of desktop toys) and ClaudeMM (a Claude Code session
# organiser) live here as proof the framework carries real UI. Their sources
# stay out of the public tree (tools/sync_public.py), so each is built only
# when its folder is present. ClaudeMM also needs nlohmann/json.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JLToys/CMakeLists.txt")
    add_subdirectory(JLToys)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ClaudeMM/CMakeLists.txt")
    if(nlohmann_json_FOUND)
        add_subdirectory(ClaudeMM)
    else()
        message(STATUS "Skipping ClaudeMM (needs nlohmann-json — "
                       "vcpkg install nlohmann-json:x64-windows).")
    endif()
endif()


set_target_properties(ChronoUIDemo PROPERTIES FOLDER "Examples")
if(TARGET WidgetTesterDemo)
    set_target_properties(WidgetTesterDemo PROPERTIES FOLDER "Examples")
endif()
set_target_properties(LayoutTester PROPERTIES FOLDER "Examples")
set_target_properties(HelloWorld PROPERTIES FOLDER "Examples")
set_target_properties(Welcome PROPERTIES FOLDER "Examples")

# ---------------------------------------------------------
# Copy the assets/ folder next to each demo's exe so they
# resolve as <exe_dir>/assets/<file> regardless of the
# current working directory at launch. Mirrors typical
# game/native-app packaging.
# ---------------------------------------------------------
set(DEMO_TARGETS ChronoUIDemo LayoutTester HelloWorld Welcome VirtualHello)
if(TARGET WidgetTesterDemo)
    list(APPEND DEMO_TARGETS WidgetTesterDemo)
endif()

# One staging target, not one post-build per demo: every demo lands in the
# same folder as ChronoUI.dll, and N parallel copies of the same tree into the
# same place raced under MSBuild /m (configure_file failing on a fresh clone).
add_custom_target(chronoui_assets
    COMMAND ${CMAKE_COMMAND}
            -DSRC=${CMAKE_SOURCE_DIR}/assets
            -DDST=$<TARGET_FILE_DIR:ChronoUI>/assets
            -P ${CMAKE_SOURCE_DIR}/cmake/StageAssets.cmake
    COMMENT "Staging assets next to the executables"
    VERBATIM
)
set_target_properties(chronoui_assets PROPERTIES FOLDER "Examples")
foreach(DEMO ${DEMO_TARGETS})
    add_dependencies(${DEMO} chronoui_assets)
endforeach()


if(MSVC)
    # Every example is a GUI app and wants the Common-Controls v6 manifest so
    # the native dialogs pick up the modern theme. One loop instead of the
    # copy-pasted pair of blocks this used to be.
    set(_chronoui_demos ChronoUIDemo LayoutTester HelloWorld Welcome
                        WidgetTesterDemo ${CHRONOUI_VIRTUAL_EXAMPLES})
    set(_chronoui_manifest
        " /MANIFESTDEPENDENCY:\"type='win32' name='Microsoft.Windows.Common-Controls'"
        " version='6.0.0.0' processorArchitecture='*'"
        " publicKeyToken='6595b64144ccf1df' language='*'\"")
    string(JOIN "" _chronoui_manifest ${_chronoui_manifest})
    foreach(_demo ${_chronoui_demos})
        if(TARGET ${_demo})
            set_property(TARGET ${_demo} PROPERTY WIN32_EXECUTABLE TRUE)
            set_property(TARGET ${_demo} APPEND_STRING PROPERTY LINK_FLAGS "${_chronoui_manifest}")
        endif()
    endforeach()
endif()
