# Reading and writing drawings.
add_library(cad4all_io STATIC
    nativeformat.h nativeformat.cpp
    dxf.h dxf.cpp
)
target_include_directories(cad4all_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cad4all_io PUBLIC cad4all_core Qt6::Core)
