find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(cad4all_tests
    geometry_test.cpp
    shapes_test.cpp
    document_test.cpp
    coordinput_test.cpp
    nativeformat_test.cpp
    dxf_test.cpp
    dwg_test.cpp
    hatch_test.cpp
    building_test.cpp
    commands_test.cpp
    editing_test.cpp
    meshexport_test.cpp
    properties_test.cpp
    recovery_test.cpp
    style_test.cpp
    block_test.cpp
)
target_link_libraries(cad4all_tests PRIVATE cad4all_core cad4all_io cad4all_engine GTest::gtest_main)
if(CAD4ALL_WITH_DWG)
    # Sample drawings that come with LibreDWG's sources.
    target_compile_definitions(cad4all_tests PRIVATE
        CAD4ALL_DWG_TEST_DATA="${libredwg_SOURCE_DIR}/test/test-data")
endif()

include(GoogleTest)
gtest_discover_tests(cad4all_tests DISCOVERY_MODE PRE_TEST)


if(CAD4ALL_BUILD_APP)
    # The panels of the main window, run on Qt's offscreen platform.
    set(app_dir ${PROJECT_SOURCE_DIR}/src/app)
    add_executable(cad4all_ui_tests
        ui_test.cpp
        ${app_dir}/infobox.cpp ${app_dir}/toolbox.cpp ${app_dir}/navigator.cpp ${app_dir}/icons.cpp ${app_dir}/stylecontrols.cpp
        ${app_dir}/infobox.h ${app_dir}/toolbox.h ${app_dir}/navigator.h ${app_dir}/stylecontrols.h
    )
    target_include_directories(cad4all_ui_tests PRIVATE ${app_dir})
    set_target_properties(cad4all_ui_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(cad4all_ui_tests PRIVATE cad4all_core cad4all_io cad4all_engine Qt6::Widgets GTest::gtest)
    gtest_discover_tests(cad4all_ui_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endif()
