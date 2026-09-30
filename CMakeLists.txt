cmake_minimum_required(VERSION 3.24)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Some MSVC toolsets report __cplusplus as C++14 even under /std:c++17, which
# breaks SFML 3's inline-static member detection (LNK2005 multiply-defined
# sf::Color::Black, sf::Time::Zero, ...). Apply /Zc:__cplusplus globally so
# the vendored SFML static libs and every consumer agree on the value.
if(MSVC)
    add_compile_options("/Zc:__cplusplus")
endif()


# Option to build the Dear ImGui desktop GUI.
option(BUILD_IMGUI_GUI "Build ImGui GUI application" ON)

# Qt-free core sources shared by the CLI and ImGui GUI.
option(BUILD_QTFREE_CORE "Build the Qt-free core library" ON)
set(LOLI_QTFREE_SRCS
    src/processrunner.cpp
    src/devicechannel.cpp
    src/eventbus.cpp
    src/appsettings.cpp
    src/threadpool.cpp
    src/loliuuid.cpp
    src/lolistream.cpp
    src/lolirecord.cpp
    src/launchdriver.cpp
    src/stacktracechannel.cpp
    src/adbtools.cpp
    src/hashstringlite.cpp
    src/pathutilslite.cpp
    src/smaps/smapssectionlite.cpp
    src/lolilogger.cpp
    src/profilecomparatorlite.cpp
    src/profilecomparison.cpp
    src/profilecomparatorlite_sqlite.cpp
    src/captureconfig.cpp
    src/symboltranslator.cpp
    src/clicapture.cpp
)
set(LOLI_QTFREE_INCS
    include/processrunner.h
    include/devicechannel.h
    include/eventbus.h
    include/appsettings.h
    include/threadpool.h
    include/loliuuid.h
    include/lolistream.h
    include/lolirecord.h
    include/launchdriver.h
    include/stacktracechannel.h
    include/adbtools.h
    include/hashstringlite.h
    include/pathutilslite.h
    include/smaps/smapssectionlite.h
    include/lolilogger.h
    include/profilecomparatorlite.h
    include/profilecomparison.h
    include/captureconfig.h
    include/symboltranslator.h
    include/clicapture.h
)

if (APPLE)
    set(OS_BUNDLE MACOSX_BUNDLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS deployment version")
    if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "13.0")
        message(FATAL_ERROR "Supported LoliProfiler macOS builds target macOS 13.0 or newer")
    endif()
    set(COMPANY "Tencent")
    set(COPYRIGHT "Copyright (c) Tencent. All rights reserved.")
    set(IDENTIFIER "com.tencent.LoliProfiler")
    set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "")
elseif (WIN32)
    set(OS_BUNDLE WIN32)
elseif(UNIX AND NOT APPLE)
    set(OS_BUNDLE "") 
endif()

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON) # Find includes in corresponding build directories
endif()

set(PROJECT_NAME LoliProfiler)
project(${PROJECT_NAME})
if(APPLE)
    # Enable in the common parent scope: SFML and NFD both compile Objective-C
    # sources, and a sibling's enable_language() does not populate this scope.
    enable_language(OBJC OBJCXX)
endif()
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # Vendored dependencies have install() rules. Keep a plain `cmake
    # --install` inside the build tree instead of writing to Program Files.
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/stage" CACHE PATH
        "Local install staging directory" FORCE)
endif()

# Qt-free since the remove-qt change: no find_package(Qt5) anywhere. The
# executables are LoliProfilerImGui, LoliProfilerCompare, and LoliProfilerCLI,
# all built from the LoliCore static library.

# Avoid generating ZERO_CHECK VS project, must in root CMakeLists.txt
if(MSVC)
    set(CMAKE_SUPPRESS_REGENERATION true)
endif()

# ============================================================================
# Qt-free core library (LoliCore)
# ============================================================================
# Shared by the GUI and headless CLI.
if(BUILD_QTFREE_CORE AND LOLI_QTFREE_SRCS)
    # Qt-free helper dependencies (vendored): SFML for DeviceChannel's TCP
    # sockets (network module enabled just for LoliCore consumers),
    # tiny-process-library sources compiled straight into LoliCore (its own
    # CMakeLists' add_definitions usage does not work with current CMake +
    # MSVC; vendored at v2.0.0, pinned submodule).
    set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_NETWORK ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(thirdparty/SFML EXCLUDE_FROM_ALL)
    if(WIN32)
        # The upstream Windows implementation opens a console window for each
        # adb/Python subprocess. Compile a build-tree copy with the Windows
        # no-window flag so the pinned submodule stays pristine.
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tiny-process-library/process_win.cpp"
             TINY_PROCESS_WIN_SOURCE)
        string(FIND "${TINY_PROCESS_WIN_SOURCE}" "TRUE, 0," TINY_PROCESS_FLAG_POS)
        if(TINY_PROCESS_FLAG_POS EQUAL -1)
            message(FATAL_ERROR "Unexpected tiny-process-library CreateProcess flags")
        endif()
        string(REPLACE "TRUE, 0," "TRUE, CREATE_NO_WINDOW,"
               TINY_PROCESS_WIN_SOURCE "${TINY_PROCESS_WIN_SOURCE}")
        set(TINY_PROCESS_SRCS "${CMAKE_CURRENT_BINARY_DIR}/tiny-process/process_win.cpp")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tiny-process")
        file(WRITE "${TINY_PROCESS_SRCS}" "${TINY_PROCESS_WIN_SOURCE}")
    else()
        set(TINY_PROCESS_SRCS thirdparty/tiny-process-library/process_unix.cpp)
    endif()
    find_package(Threads REQUIRED)
    add_library(LoliCore STATIC
        ${LOLI_QTFREE_SRCS}
        ${LOLI_QTFREE_INCS}
        src/lz4/lz4.c
        thirdparty/sqlite/sqlite3.c
        thirdparty/tiny-process-library/process.cpp
        ${TINY_PROCESS_SRCS}
    )
    set_target_properties(LoliCore PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_include_directories(LoliCore
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tiny-process-library>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rapidjson/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite>
        PRIVATE
            src/lz4/
    )
    if(MSVC)
        target_compile_options(LoliCore PRIVATE "/W3" "/EHsc" "/MP")
        target_compile_definitions(LoliCore PRIVATE "_CRT_SECURE_NO_WARNINGS=1" "NOGDI" "NOMINMAX")
    else()
        target_compile_options(LoliCore PRIVATE "-Wall" "-Wextra" $<$<COMPILE_LANGUAGE:CXX>:-Wno-invalid-offsetof;-fno-rtti> "-Wno-missing-braces")
    endif()
    target_link_libraries(LoliCore PUBLIC Threads::Threads sfml-network)
    if(WIN32)
        target_link_libraries(LoliCore PUBLIC psapi)
    endif()
    # Consumers append "LoliCore" to their target_link_libraries call.

endif()



# ============================================================================
# CLI executable, Qt-free (LoliProfilerCLI)
# ============================================================================
# Headless capture + --dump/--compare entirely on LoliCore (design D12: the
# same capture APIs the ImGui GUI consumes).
option(BUILD_QTFREE_CLI "Build the Qt-free CLI executable" ON)
if(BUILD_QTFREE_CLI AND BUILD_QTFREE_CORE AND LOLI_QTFREE_SRCS)
    add_executable(LoliProfilerCLI
        src/main_cli2.cpp
        src/cli_fileops.cpp
    )
    if(WIN32)
        target_sources(LoliProfilerCLI PRIVATE windows.rc)
    endif()
    set_target_properties(LoliProfilerCLI PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    if(MSVC)
        # /Zc:__cplusplus: older MSVC reports __cplusplus as C++14 even under
        # /std:c++17, which breaks SFML 3's inline-static member detection
        # (LNK2005 multiply-defined sf::Color::Black etc.).
        target_compile_options(LoliProfilerCLI PRIVATE "/W3" "/EHsc" "/MP" "/Zc:__cplusplus")
        target_compile_definitions(LoliProfilerCLI PRIVATE "_CRT_SECURE_NO_WARNINGS=1" "NOGDI" "NOMINMAX")
    else()
        target_compile_options(LoliProfilerCLI PRIVATE "-Wall" "-Wextra" "-Wno-missing-braces")
    endif()
    target_link_libraries(LoliProfilerCLI PRIVATE LoliCore)
    if(WIN32)
        target_link_libraries(LoliProfilerCLI PRIVATE shell32)
    endif()
    install(TARGETS LoliProfilerCLI DESTINATION ${CMAKE_BINARY_DIR}/bin/release)
endif()

# ============================================================================
# ImGui GUI Application (LoliProfilerImGui) - SFML + Dear ImGui
# ============================================================================
if(BUILD_IMGUI_GUI)

# Qt-free since the remove-qt change (task 6.x): the bridge consumes
# LoliCore directly. No find_package(Qt5) here anymore.

# --- Third-party dependencies (vendored under thirdparty/) ---

# SFML 3 (static, no audio; network enabled for LoliCore's DeviceChannel).
# May already be added by the LoliCore block above - add_subdirectory must
# run only once.
if(NOT TARGET sfml-graphics)
    set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_NETWORK ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(thirdparty/SFML)
endif()

# ImGui-SFML v3 (expects IMGUI_DIR pointing at the imgui top-level dir,
# and IMGUI_SFML_FIND_SFML=OFF so it uses the SFML target from add_subdirectory)
set(IMGUI_SFML_FIND_SFML OFF CACHE BOOL "" FORCE)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui CACHE PATH "imgui top-level directory" FORCE)
add_subdirectory(thirdparty/imgui-sfml)

# nativefiledialog-extended (target: nfd)
add_subdirectory(thirdparty/nativefiledialog-extended)

# --- Sources ---
# NOTE: add new GUI source files to this list as they are created
# (file(GLOB src/gui/*.cpp) could be used instead, explicit list matches repo style).
# ImGui core sources (imgui/imgui_demo/imgui_draw/imgui_tables/imgui_widgets) are
# compiled into the ImGui-SFML target itself via its FindImGui.cmake; only the
# OpenGL3 backend is compiled here.
set(IMGUI_GUI_SRCS
    src/gui/main_imgui.cpp
    src/gui/comparelaunch.cpp
    src/gui/themes.cpp
    src/gui/filedialogs.cpp
    src/gui/guidatabridge.cpp
    src/gui/stacktracetree.cpp
    src/gui/leakdetect.cpp
    src/gui/runlaunchdialog.cpp
    src/gui/captureconfigdialog.cpp
    src/gui/chartwidgets.cpp
    src/gui/treemappanel.cpp
    thirdparty/imgui/backends/imgui_impl_opengl3.cpp
)

# All core services (adb, sockets, serializer, launch driver, config) come
# from LoliCore via target_link_libraries.
set(IMGUI_GUI_INCS
    src/gui/guisnapshot.h
    src/gui/chartwidgets.h
    src/gui/guidatabridge.h
    src/gui/stacktracetree.h
    src/gui/leakdetect.h
    src/gui/runlaunchdialog.h
    src/gui/captureconfigdialog.h
    src/gui/treemappanel.h
    src/gui/themes.h
    src/gui/filedialogs.h
    src/gui/shortcuts.h
)

add_executable(LoliProfilerImGui ${OS_BUNDLE}
    ${IMGUI_GUI_SRCS}
    ${IMGUI_GUI_INCS}
)
if(WIN32)
    target_sources(LoliProfilerImGui PRIVATE windows.rc)
elseif(APPLE)
    set_source_files_properties(res/loli_cat_icon.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    target_sources(LoliProfilerImGui PRIVATE res/loli_cat_icon.icns)
    set_target_properties(LoliProfilerImGui PROPERTIES
        MACOSX_BUNDLE_ICON_FILE loli_cat_icon.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER "${IDENTIFIER}"
        MACOSX_BUNDLE_BUNDLE_NAME "LoliProfiler"
        MACOSX_BUNDLE_COPYRIGHT "${COPYRIGHT}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macosx_bundle_info.plist.in")
endif()
if(APPLE)
    set(LOLI_GUI_RUNTIME_DIR "$<TARGET_FILE_DIR:LoliProfilerImGui>/../Resources")
else()
    set(LOLI_GUI_RUNTIME_DIR "$<TARGET_FILE_DIR:LoliProfilerImGui>")
endif()
add_custom_command(TARGET LoliProfilerImGui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LOLI_GUI_RUNTIME_DIR}/res"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/res/loli_cat_icon.png"
        "${LOLI_GUI_RUNTIME_DIR}/res/loli_cat_icon.png"
    VERBATIM)

# C++17 is required by SFML 3 / ImGui-SFML (do NOT change the global standard)
set_target_properties(LoliProfilerImGui PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

if(MSVC)
    # No /WX here: vendored deps are compiled as-is and would trip -Werror-style builds
    # /Zc:__cplusplus: see LoliProfilerCLI note (older MSVC misreport).
    target_compile_options(LoliProfilerImGui PRIVATE "/W3" "/EHsc" "/MP" "/Zc:__cplusplus")
    target_compile_definitions(LoliProfilerImGui PRIVATE "_CRT_SECURE_NO_WARNINGS=1" "NOGDI" "NOMINMAX" "NO_GUI_MODE")
else()
    target_compile_options(LoliProfilerImGui PRIVATE "-Wall" "-Wextra" $<$<COMPILE_LANGUAGE:CXX>:-Wno-invalid-offsetof;-fno-rtti> "-Wno-missing-braces")
    target_compile_definitions(LoliProfilerImGui PRIVATE "NO_GUI_MODE")
endif()

target_include_directories(LoliProfilerImGui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        src/
        thirdparty/imgui
)

target_link_libraries(LoliProfilerImGui
    PRIVATE
        LoliCore
        SFML::Graphics
        SFML::Window
        SFML::System
        ImGui-SFML::ImGui-SFML
        nfd
)
if(TARGET sfml-main)
    target_link_libraries(LoliProfilerImGui PRIVATE sfml-main)
endif()

if(WIN32)
    target_link_libraries(LoliProfilerImGui PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(LoliProfilerImGui PRIVATE "-framework OpenGL")
endif()

if(APPLE AND NOT XCODE_VERSION)
    add_custom_command(TARGET LoliProfilerImGui
        POST_BUILD COMMAND
        ${CMAKE_INSTALL_NAME_TOOL} -add_rpath "@executable_path/../Frameworks/"
        $<TARGET_FILE:LoliProfilerImGui>)
endif()

install(TARGETS LoliProfilerImGui DESTINATION ${CMAKE_BINARY_DIR}/bin/release)
install(FILES res/loli_cat_icon.png DESTINATION ${CMAKE_BINARY_DIR}/bin/release/res)

# Standalone desktop comparison shares the same LoliCore API as --compare.
if(WIN32)
    set(COMPARE_BUNDLE WIN32)
else()
    set(COMPARE_BUNDLE "")
endif()
add_executable(LoliProfilerCompare ${COMPARE_BUNDLE}
    src/gui/main_compare.cpp
    src/gui/comparetreestate.cpp
    src/gui/comparepanel.cpp
    src/gui/filedialogs.cpp
    src/gui/themes.cpp)
set_target_properties(LoliProfilerCompare PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(LoliProfilerCompare PRIVATE LoliCore SFML::Graphics SFML::Window SFML::System
    ImGui-SFML::ImGui-SFML nfd)
if(TARGET sfml-main)
    target_link_libraries(LoliProfilerCompare PRIVATE sfml-main)
endif()
if(MSVC)
    target_compile_options(LoliProfilerCompare PRIVATE /W3 /EHsc /MP)
    target_compile_definitions(LoliProfilerCompare PRIVATE _CRT_SECURE_NO_WARNINGS=1 NOMINMAX)
endif()
if(WIN32)
    target_sources(LoliProfilerCompare PRIVATE windows.rc)
    target_link_libraries(LoliProfilerCompare PRIVATE opengl32 shell32)
elseif(APPLE)
    target_link_libraries(LoliProfilerCompare PRIVATE "-framework OpenGL")
endif()
install(TARGETS LoliProfilerCompare DESTINATION ${CMAKE_BINARY_DIR}/bin/release)
add_dependencies(LoliProfilerImGui LoliProfilerCompare)

endif() # BUILD_IMGUI_GUI

option(BUILD_COMPARISON_TESTS "Build focused comparison regression tests" OFF)
if(BUILD_COMPARISON_TESTS)
    enable_testing()
    add_executable(LoliComparisonTests tests/comparison_tests.cpp src/gui/comparetreestate.cpp src/gui/comparelaunch.cpp)
    target_include_directories(LoliComparisonTests PRIVATE src/gui)
    target_link_libraries(LoliComparisonTests PRIVATE LoliCore)
    set_target_properties(LoliComparisonTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME comparison-regressions COMMAND LoliComparisonTests)
endif()

# Focused host-path regressions; independent of an attached Android device.
option(BUILD_PLATFORM_TESTS "Build platform tool/runtime path regression tests" OFF)
if(BUILD_PLATFORM_TESTS)
    enable_testing()
    add_executable(LoliPlatformPathsTest tests/platform_paths.cpp)
    set_target_properties(LoliPlatformPathsTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    target_link_libraries(LoliPlatformPathsTest PRIVATE LoliCore)
    add_test(NAME platform_paths COMMAND LoliPlatformPathsTest)
    add_executable(LoliCallTreeTest tests/calltree_identity.cpp)
    set_target_properties(LoliCallTreeTest PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    target_link_libraries(LoliCallTreeTest PRIVATE LoliCore)
    add_test(NAME calltree_identity COMMAND LoliCallTreeTest)
    set_tests_properties(calltree_identity PROPERTIES TIMEOUT 30)
    if(TARGET ImGui-SFML::ImGui-SFML)
        add_executable(LoliGuiShortcutsTest tests/gui_shortcuts.cpp)
        set_target_properties(LoliGuiShortcutsTest PROPERTIES
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
        target_include_directories(LoliGuiShortcutsTest PRIVATE src/gui)
        target_link_libraries(LoliGuiShortcutsTest PRIVATE ImGui-SFML::ImGui-SFML)
        add_test(NAME gui_shortcuts COMMAND LoliGuiShortcutsTest)
        set_tests_properties(gui_shortcuts PROPERTIES TIMEOUT 30)
    endif()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME jdwp_injector COMMAND "${Python3_EXECUTABLE}"
            -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests"
            -p test_jdwp_injector.py)
        set_tests_properties(jdwp_injector PROPERTIES TIMEOUT 30)
    endif()
endif()
