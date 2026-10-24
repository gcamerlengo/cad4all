# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.28)

project(CAD4All
    VERSION 0.1.0
    DESCRIPTION "CAD 2D e 3D parametrico open source"
    HOMEPAGE_URL "https://github.com/gcamerlengo/cad4all"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CAD4ALL_BUILD_APP "Build the desktop application" ON)
option(CAD4ALL_BUILD_TESTS "Build the unit tests" ON)
option(CAD4ALL_WITH_DWG "Read DWG files with LibreDWG (downloaded and built with CAD4All)" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core)
if(CAD4ALL_BUILD_APP)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Gui Widgets PrintSupport)
endif()
qt_standard_project_setup()

if(CAD4ALL_WITH_DWG)
    # LibreDWG (GPL 3) is built from source as a static library: only the
    # reader and the DXF writer, no programs, no tests. To use a local copy,
    # configure with -DFETCHCONTENT_SOURCE_DIR_LIBREDWG=/path/to/libredwg.
    enable_language(C)
    include(FetchContent)
    set(LIBREDWG_LIBONLY ON)
    set(LIBREDWG_DISABLE_JSON ON)
    set(LIBREDWG_DISABLE_WRITE ON)
    set(ENABLE_LTO OFF)
    set(DISABLE_WERROR ON)
    FetchContent_Declare(libredwg
        GIT_REPOSITORY https://github.com/LibreDWG/libredwg.git
        GIT_TAG 0.14
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(libredwg)
    # The library target is called libredwg with MSVC, redwg elsewhere.
    if(TARGET libredwg)
        set(CAD4ALL_LIBREDWG_TARGET libredwg)
    else()
        set(CAD4ALL_LIBREDWG_TARGET redwg)
    endif()
endif()

# Our warning level, set after LibreDWG so it applies to our code only.
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/core)
add_subdirectory(src/io)
add_subdirectory(src/engine)
if(CAD4ALL_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(CAD4ALL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
