cmake_minimum_required(VERSION 3.15)

project(ChronoUIProject LANGUAGES CXX)

# ---------------------------------------------------------
# CRITICAL FIX 1: Enable Solution Folders
# ---------------------------------------------------------
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Force C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Add todo.txt to Solution Root ---
set(SOLUTION_FILES
    assets/bootstrap_lite.css
    todo.txt
    readme.md
)
add_custom_target(SolutionItems SOURCES ${SOLUTION_FILES})
set_target_properties(SolutionItems PROPERTIES FOLDER "Solution Items")

# Global Include Directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/widgets)

# MSVC Specific Flags
if(MSVC)
    add_compile_options(/W4 /permissive- /D_CRT_SECURE_NO_WARNINGS)
    add_compile_definitions(UNICODE _UNICODE)

    # Enable "Apply Code Changes" (Edit And Continue)
    string(REPLACE "/Zi" "/ZI" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    add_link_options($<$<CONFIG:Debug>:/EDITANDCONTINUE>)
endif() 

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
add_executable(WidgetTesterDemo WIN32 
                src/examples/WidgetTesterDemo.cpp 
                include/virtual_drive.hpp
) 
add_executable(LayoutTester WIN32 
                src/examples/LayoutTester.cpp 
                include/virtual_drive.hpp
) 
add_executable(HelloWorld WIN32 
                src/examples/HelloWorld.cpp 
                include/virtual_drive.hpp
) 

# REQ: All exes depend of chronoui and widgets
# Added ${ALL_WIDGET_TARGETS} to the linking list. 
# This ensures CMake builds widgets before exes, and links the import libs.
target_link_libraries(ChronoUIDemo PRIVATE ChronoUI ${ALL_WIDGET_TARGETS})
target_link_libraries(WidgetTesterDemo PRIVATE ChronoUI ${ALL_WIDGET_TARGETS})
target_link_libraries(LayoutTester PRIVATE ChronoUI ${ALL_WIDGET_TARGETS})
target_link_libraries(HelloWorld PRIVATE ChronoUI ${ALL_WIDGET_TARGETS})

set_target_properties(ChronoUIDemo PROPERTIES FOLDER "Examples")
set_target_properties(WidgetTesterDemo PROPERTIES FOLDER "Examples")
set_target_properties(LayoutTester PROPERTIES FOLDER "Examples")
set_target_properties(HelloWorld PROPERTIES FOLDER "Examples")


if(MSVC)
    set_property(TARGET ChronoUIDemo PROPERTY WIN32_EXECUTABLE TRUE)
    set_property(TARGET WidgetTesterDemo PROPERTY WIN32_EXECUTABLE TRUE)
    set_property(TARGET LayoutTester PROPERTY WIN32_EXECUTABLE TRUE)
    set_property(TARGET HelloWorld PROPERTY WIN32_EXECUTABLE TRUE)
endif()

if(MSVC)
    set_property(TARGET ChronoUIDemo APPEND_STRING PROPERTY LINK_FLAGS 
        " /MANIFESTDEPENDENCY:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\""
    )
endif()