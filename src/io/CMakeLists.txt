# Reading and writing drawings.
add_library(cad4all_io STATIC
    nativeformat.h nativeformat.cpp
    dxf.h dxf.cpp
    dwgreader.h dwgreader.cpp
    meshexport.h meshexport.cpp
    recovery.h recovery.cpp
)
target_include_directories(cad4all_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cad4all_io PUBLIC cad4all_core Qt6::Core)

if(CAD4ALL_WITH_DWG)
    # The C shim needs LibreDWG's private headers and its generated config.h.
    target_sources(cad4all_io PRIVATE dwg_shim.h dwg_shim.c)
    set_source_files_properties(dwg_shim.c PROPERTIES INCLUDE_DIRECTORIES
        "${libredwg_SOURCE_DIR}/include;${libredwg_SOURCE_DIR}/src;${libredwg_BINARY_DIR}/src")
    target_compile_definitions(cad4all_io PRIVATE CAD4ALL_HAVE_DWG)
    target_link_libraries(cad4all_io PRIVATE ${CAD4ALL_LIBREDWG_TARGET})
endif()
