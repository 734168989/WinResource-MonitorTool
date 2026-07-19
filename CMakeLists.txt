cmake_minimum_required(VERSION 3.16)
project(挂机电脑资源监测软件 VERSION 3.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Windows libraries
# ============================================================================
set(WIN32_LIBS
    comctl32 comdlg32 ole32 oleaut32 gdi32 user32 kernel32
    iphlpapi pdh ws2_32 uuid shell32
)

# ============================================================================
# Source files
# ============================================================================
set(SOURCES
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/HelpDialog.cpp
    src/utils/DataBuffer.cpp
    src/utils/ConfigManager.cpp
    src/utils/ExcelExporter.cpp
    src/utils/HtmlChartExporter.cpp
    src/core/SystemMonitor.cpp
    src/core/ProcessMonitor.cpp
    src/core/NetSpeedMonitor.cpp
)

set(HEADERS
    include/ui/MainWindow.h
    include/ui/HelpDialog.h
    include/utils/DataModels.h
    include/utils/DataBuffer.h
    include/utils/ConfigManager.h
    include/utils/ExcelExporter.h
    include/utils/HtmlChartExporter.h
    include/core/SystemMonitor.h
    include/core/ProcessMonitor.h
    include/core/NetSpeedMonitor.h
    res/resource.h
)

set(RESOURCES
    res/monitor.rc
)

# ============================================================================
# Executable
# ============================================================================
add_executable(MonitorTool WIN32
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# ============================================================================
# Target-specific settings
# ============================================================================
target_compile_definitions(MonitorTool PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0601
)

target_include_directories(MonitorTool PRIVATE include include/core include/ui include/utils src res)

target_link_libraries(MonitorTool ${WIN32_LIBS})

set_target_properties(MonitorTool PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# Disable default MSVC manifest, use custom admin-required manifest via mt.exe
if(MSVC)
    target_link_options(MonitorTool PRIVATE "/MANIFEST:NO")
    add_custom_command(TARGET MonitorTool POST_BUILD
        COMMAND "mt.exe" -nologo
                -manifest "${CMAKE_SOURCE_DIR}/res/MonitorTool.manifest"
                "-outputresource:$<TARGET_FILE:MonitorTool>;#1"
        COMMENT "Embedding custom manifest (requireAdministrator)"
    )
endif()

# ============================================================================
# Compiler-specific options
# ============================================================================
if(MSVC)
    # ---- MSVC (Visual Studio) ----
    # .rc file is UTF-8; resource compiler needs codepage hint + src/include paths
    set(CMAKE_RC_FLAGS "/c65001 /I src /I res" CACHE STRING "UTF-8 resource compiler flag" FORCE)

    target_compile_options(MonitorTool PRIVATE
        /W4              # Warning level 4
        /utf-8           # UTF-8 source encoding
        /MP              # Multi-processor compilation
        /permissive-     # Standards conformance
        /Zc:__cplusplus  # Correct __cplusplus macro
    )

    # Debug: full debug info, no optimization
    target_compile_options(MonitorTool PRIVATE
        $<$<CONFIG:Debug>:/Od /ZI /RTC1>
    )

    # Release: optimize for size and speed
    target_compile_options(MonitorTool PRIVATE
        $<$<CONFIG:Release>:/O2 /Gy /GL>
    )
    target_link_options(MonitorTool PRIVATE
        $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /LTCG>
    )

    # RelWithDebInfo: optimized + debug symbols (recommended for daily use)
    target_compile_options(MonitorTool PRIVATE
        $<$<CONFIG:RelWithDebInfo>:/O2 /Zi /Gy>
    )
    target_link_options(MonitorTool PRIVATE
        $<$<CONFIG:RelWithDebInfo>:/OPT:REF /OPT:ICF /DEBUG:FULL>
    )

else()
    # ---- GCC / MinGW ----
    target_compile_options(MonitorTool PRIVATE
        -Wall -Wextra
    )

    target_compile_options(MonitorTool PRIVATE
        $<$<CONFIG:Debug>:-Og -g3>
    )

    target_compile_options(MonitorTool PRIVATE
        $<$<CONFIG:Release>:-O2 -s -ffunction-sections -fdata-sections>
    )
    target_link_options(MonitorTool PRIVATE
        $<$<CONFIG:Release>:-Wl,--gc-sections -s>
    )

    target_compile_options(MonitorTool PRIVATE
        $<$<CONFIG:RelWithDebInfo>:-O2 -g -ffunction-sections -fdata-sections>
    )
    target_link_options(MonitorTool PRIVATE
        $<$<CONFIG:RelWithDebInfo>:-Wl,--gc-sections>
    )
endif()


# ============================================================================
# Output directories
# ============================================================================
set_target_properties(MonitorTool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG              "${CMAKE_SOURCE_DIR}/out"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE            "${CMAKE_SOURCE_DIR}/out"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO     "${CMAKE_SOURCE_DIR}/out"
)

# ============================================================================
# Source group for Visual Studio filters
# ============================================================================
source_group("Source Files\\core"  FILES src/core/SystemMonitor.cpp src/core/ProcessMonitor.cpp src/core/NetSpeedMonitor.cpp)
source_group("Source Files\\ui"    FILES src/ui/MainWindow.cpp src/ui/HelpDialog.cpp)
source_group("Source Files\\utils" FILES src/utils/DataBuffer.cpp src/utils/ConfigManager.cpp src/utils/ExcelExporter.cpp src/utils/HtmlChartExporter.cpp)
source_group("Source Files"        FILES src/main.cpp)
source_group("Header Files\\core"  FILES include/core/SystemMonitor.h include/core/ProcessMonitor.h include/core/NetSpeedMonitor.h)
source_group("Header Files\\ui"    FILES include/ui/MainWindow.h include/ui/HelpDialog.h)
source_group("Header Files\\utils" FILES include/utils/DataModels.h include/utils/DataBuffer.h include/utils/ConfigManager.h include/utils/ExcelExporter.h include/utils/HtmlChartExporter.h)
source_group("Header Files"        FILES )
source_group("Resource Files"      FILES ${RESOURCES} res/resource.h)
