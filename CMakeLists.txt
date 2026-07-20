cmake_minimum_required(VERSION 3.16)
project(挂机电脑资源监测软件 VERSION 3.5 LANGUAGES CXX)

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
    src/utils/DataLogger.cpp
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
    include/utils/DataLogger.h
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
# Target 1: MonitorTool — 完整时间戳 (开发/调试用)
# ============================================================================
add_executable(MonitorTool WIN32
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# ============================================================================
# Target 2: MonitorTool_Release — 年月时间戳 (发布用)
# ============================================================================
add_executable(MonitorTool_Release WIN32
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)
target_compile_definitions(MonitorTool_Release PRIVATE TIMESTAMP_SHORT)

# ============================================================================
# Common target settings
# ============================================================================
foreach(TGT MonitorTool MonitorTool_Release)
    target_compile_definitions(${TGT} PRIVATE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN NOMINMAX
        _WIN32_WINNT=0x0601
    )
    target_include_directories(${TGT} PRIVATE include include/core include/ui include/utils src res)
    target_link_libraries(${TGT} ${WIN32_LIBS})
    set_target_properties(${TGT} PROPERTIES WIN32_EXECUTABLE TRUE)
endforeach()

# ============================================================================
# Build timestamp — placeholder written at configure time.
# The actual timestamp is binary-patched into the exe by POST_BUILD
# (powershell cmake/patch_timestamp.ps1) → accurate to the second.
# ============================================================================
file(WRITE "${CMAKE_SOURCE_DIR}/include/utils/build_timestamp.h"
    "#pragma once\n#define BUILD_TIMESTAMP_FULL  L\"0000-00-00 00:00:00\"\n#define BUILD_TIMESTAMP_SHORT L\"0000-00\"\n"
)

if(MSVC)
    target_link_options(MonitorTool PRIVATE "/MANIFEST:NO")
    target_link_options(MonitorTool_Release PRIVATE "/MANIFEST:NO")

    # POST_BUILD: embed manifest, then patch timestamp placeholder → real time
    add_custom_command(TARGET MonitorTool POST_BUILD
        COMMAND "mt.exe" -nologo
                -manifest "${CMAKE_SOURCE_DIR}/res/MonitorTool.manifest"
                "-outputresource:$<TARGET_FILE:MonitorTool>;#1"
        COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/cmake/patch_timestamp.ps1" -ExeFile "$<TARGET_FILE:MonitorTool>"
        COMMENT "Embed manifest + patch timestamp → MonitorTool"
    )
    add_custom_command(TARGET MonitorTool_Release POST_BUILD
        COMMAND "mt.exe" -nologo
                -manifest "${CMAKE_SOURCE_DIR}/res/MonitorTool.manifest"
                "-outputresource:$<TARGET_FILE:MonitorTool_Release>;#1"
        COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/cmake/patch_timestamp.ps1" -ExeFile "$<TARGET_FILE:MonitorTool_Release>"
        COMMENT "Embed manifest + patch timestamp → MonitorTool_Release"
    )
endif()

# ============================================================================
# Compiler-specific options
# ============================================================================
if(MSVC)
    set(CMAKE_RC_FLAGS "/c65001 /I src /I res" CACHE STRING "UTF-8 resource compiler flag" FORCE)

    foreach(TGT MonitorTool MonitorTool_Release)
        target_compile_options(${TGT} PRIVATE
            /W4 /utf-8 /MP /permissive- /Zc:__cplusplus
        )
        target_compile_options(${TGT} PRIVATE
            $<$<CONFIG:Debug>:/Od /ZI /RTC1>
            $<$<CONFIG:Release>:/O2 /Gy /GL>
            $<$<CONFIG:RelWithDebInfo>:/O2 /Zi /Gy>
        )
        target_link_options(${TGT} PRIVATE
            $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /LTCG>
            $<$<CONFIG:RelWithDebInfo>:/OPT:REF /OPT:ICF /DEBUG:FULL>
        )
    endforeach()
else()
    foreach(TGT MonitorTool MonitorTool_Release)
        target_compile_options(${TGT} PRIVATE -Wall -Wextra)
        target_compile_options(${TGT} PRIVATE
            $<$<CONFIG:Debug>:-Og -g3>
            $<$<CONFIG:Release>:-O2 -s -ffunction-sections -fdata-sections>
            $<$<CONFIG:RelWithDebInfo>:-O2 -g -ffunction-sections -fdata-sections>
        )
        target_link_options(${TGT} PRIVATE
            $<$<CONFIG:Release>:-Wl,--gc-sections -s>
            $<$<CONFIG:RelWithDebInfo>:-Wl,--gc-sections>
        )
    endforeach()
endif()

# ============================================================================
# Output directories
# ============================================================================
foreach(TGT MonitorTool MonitorTool_Release)
    set_target_properties(${TGT} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG              "${CMAKE_SOURCE_DIR}/out"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE            "${CMAKE_SOURCE_DIR}/out"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO     "${CMAKE_SOURCE_DIR}/out"
    )
endforeach()

# Release 目标输出为 MonitorTool_Release.exe，静态链接 CRT 避免缺少 DLL
set_target_properties(MonitorTool_Release PROPERTIES
    OUTPUT_NAME "MonitorTool_Release"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
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
source_group("Header Files\\utils" FILES include/utils/DataModels.h include/utils/DataBuffer.h include/utils/ConfigManager.h include utils/ExcelExporter.h include/utils/HtmlChartExporter.h)
source_group("Header Files"        FILES )
source_group("Resource Files"      FILES ${RESOURCES} res/resource.h)
