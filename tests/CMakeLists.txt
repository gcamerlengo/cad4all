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
    commands_test.cpp
    editing_test.cpp
)
target_link_libraries(cad4all_tests PRIVATE cad4all_core cad4all_io cad4all_engine GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(cad4all_tests DISCOVERY_MODE PRE_TEST)
