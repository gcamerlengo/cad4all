# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)

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

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core)
if(CAD4ALL_BUILD_APP)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Gui Widgets PrintSupport)
endif()
qt_standard_project_setup()

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
