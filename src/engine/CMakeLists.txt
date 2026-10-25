# Command engine and commands. Depends only on QtCore so it can be tested
# without a window.
add_library(cad4all_engine STATIC
    commandengine.h commandengine.cpp
    commands.h commands.cpp
    properties.h properties.cpp
)
target_include_directories(cad4all_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cad4all_engine PUBLIC cad4all_core Qt6::Core)
