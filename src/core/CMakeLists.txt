# Pure C++ core: geometry, shapes and the drawing document. No Qt here, so the
# same code can later be reused by command line tools and other front ends.
add_library(cad4all_core STATIC
    geometry.h geometry.cpp
    shapes.h shapes.cpp
    document.h document.cpp
    coordinput.h coordinput.cpp
    editing.h editing.cpp
    hatch.h hatch.cpp
    building.h building.cpp
)
target_include_directories(cad4all_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
