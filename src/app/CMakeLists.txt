qt_add_executable(cad4all
    main.cpp
    mainwindow.h mainwindow.cpp
    cadview.h cadview.cpp
    drawingpainter.h drawingpainter.cpp
    plot.h plot.cpp
    plotdialog.h plotdialog.cpp
    render3d.h render3d.cpp
    view3d.h view3d.cpp
    commandline.h commandline.cpp
    layerpanel.h layerpanel.cpp
    icons.h icons.cpp
    ribbon.h ribbon.cpp
    toolbox.h toolbox.cpp
    infobox.h infobox.cpp
    navigator.h navigator.cpp
)
target_link_libraries(cad4all PRIVATE cad4all_core cad4all_io cad4all_engine Qt6::Widgets Qt6::PrintSupport)
target_compile_definitions(cad4all PRIVATE CAD4ALL_VERSION="${PROJECT_VERSION}")

set_target_properties(cad4all PROPERTIES
    OUTPUT_NAME CAD4All
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_GUI_IDENTIFIER org.cad4all.CAD4All
    MACOSX_BUNDLE_BUNDLE_NAME CAD4All
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

include(GNUInstallDirs)
install(TARGETS cad4all
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
