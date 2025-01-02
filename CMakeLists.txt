cmake_minimum_required(VERSION 3.2)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (APPLE)
    set(OS_BUNDLE MACOSX_BUNDLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")
    set(COMPANY "Tencent")
    set(COPYRIGHT "Copyright (c) Tencent. All rights reserved.")
    set(IDENTIFIER "com.tencent.LoliProfiler")
   set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "")
elseif (WIN32)
    set(OS_BUNDLE WIN32)
endif()

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON) # Find includes in corresponding build directories
endif()

set(CMAKE_AUTOMOC ON) # For meta object compiler
set(CMAKE_AUTORCC ON) # Resource files
set(CMAKE_AUTOUIC ON) # UI files
set(PROJECT_NAME LoliProfiler)
project(${PROJECT_NAME})

find_package(Qt5 COMPONENTS Core Widgets OpenGL Gui Network Charts Concurrent REQUIRED)

# Avoid generating ZERO_CHECK VS project, must in root CMakeLists.txt
if(MSVC)
    set(CMAKE_SUPPRESS_REGENERATION true)
endif()

set(LOLIPROFILER_INCS
    include/adbprocess.h
    include/addressprocess.h
    include/charttooltipitem.h
    include/configdialog.h
    include/configlistwidget.h
    include/customgraphicsview.h
    include/fixedscrollarea.h
    include/interactivechartview.h
    include/pathutils.h
    include/selectappdialog.h
    include/smaps/smapssection.h
    include/smaps/statsmapsdialog.h
    include/smaps/visualizesmapsdialog.h
    src/lz4/lz4.h
    include/mainwindow.h
    include/memgraphicsview.h
    include/meminfoprocess.h
    include/screenshotprocess.h
    include/stacktracemodel.h
    include/stacktraceprocess.h
    include/stacktraceproxymodel.h
    include/startappprocess.h
    include/timeprofiler.h
    include/treemapgraphicsview.h
    include/hashstring.h
)

set(LOLIPROFILER_SRCS 
    src/configlistwidget.cpp
    src/smaps/statsmapsdialog.cpp
    src/smaps/visualizesmapsdialog.cpp
    src/adbprocess.cpp
    src/addressprocess.cpp
    src/charttooltipitem.cpp
    src/configdialog.cpp
    src/customgraphicsview.cpp
    src/fixedscrollarea.cpp
    src/interactivechartview.cpp
    src/lz4/lz4.c
    src/main.cpp
    src/mainwindow.cpp
    src/memgraphicsview.cpp
    src/meminfoprocess.cpp
    src/pathutils.cpp
    src/screenshotprocess.cpp
    src/selectappdialog.cpp
    src/stacktracemodel.cpp
    src/stacktraceprocess.cpp
    src/stacktraceproxymodel.cpp
    src/startappprocess.cpp
    src/treemapgraphicsview.cpp
    src/hashstring.cpp
)

set(LOLIPROFILER_UIS
    src/mainwindow.ui
    src/configdialog.ui
)

set(LOLIPROFILER_RESES 
    res/icon.qrc
    res/devices.icns
)

if(MSVC)
    list(APPEND LOLIPROFILER_RESES qdarkstyle/dark/darkstyle.qrc)
endif()

if (APPLE)
    set(ICON_FILE res/icon.icns)
    set_source_files_properties(${ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    # Identify MacOS bundle
    set(MACOSX_BUNDLE_BUNDLE_NAME "Tencent LoliProfiler")
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_LONG_VERSION_STRING ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
    set(MACOSX_BUNDLE_COPYRIGHT ${COPYRIGHT})
    set(MACOSX_BUNDLE_GUI_IDENTIFIER ${IDENTIFIER})
    set(MACOSX_BUNDLE_ICON_FILE devices.icns)
endif()

add_executable(${PROJECT_NAME} ${OS_BUNDLE} 
    ${LOLIPROFILER_SRCS} ${LOLIPROFILER_INCS} ${LOLIPROFILER_UIS} ${META_FILES_TO_INCLUDE} ${LOLIPROFILER_RESES} windows.rc)

if (APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_LIST_DIR}/cmake/macosx_bundle_info.plist.in)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "/W3" "/EHsc" "/WX" "/MP")
    target_compile_definitions(${PROJECT_NAME} PRIVATE "_CRT_SECURE_NO_WARNINGS=1" "NOGDI" "NOMINMAX") 
else()
    target_compile_options(${PROJECT_NAME} PRIVATE "-Wall" "-Wextra" "-fno-rtti" "-Werror" "-Wno-invalid-offsetof" "-Wno-missing-braces")
endif()

target_compile_definitions(${PROJECT_NAME} 
    PUBLIC 
        ${Qt5Widgets_DEFINITIONS} 
        QT_DEPRECATED_WARNINGS
    PRIVATE
)

target_include_directories(${PROJECT_NAME}
    PUBLIC 
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        src/
        src/lz4/
)

target_link_libraries(${PROJECT_NAME} 
    Qt5::Core
    Qt5::Widgets
    Qt5::Gui
    Qt5::Network
    Qt5::Charts
    Qt5::OpenGL
    Qt5::Concurrent
)

if(APPLE AND NOT XCODE_VERSION)
    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD COMMAND 
        ${CMAKE_INSTALL_NAME_TOOL} -add_rpath "@executable_path/../Frameworks/"
        $<TARGET_FILE:${PROJECT_NAME}>)
endif()

install(TARGETS ${PROJECT_NAME}  DESTINATION ${CMAKE_BINARY_DIR}/bin/release)
