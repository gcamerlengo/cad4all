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
    layoutpanel.h layoutpanel.cpp
    stylecontrols.h stylecontrols.cpp
    icons.h icons.cpp
    ribbon.h ribbon.cpp
    toolbox.h toolbox.cpp
    infobox.h infobox.cpp
    navigator.h navigator.cpp
)
target_link_libraries(cad4all PRIVATE cad4all_core cad4all_io cad4all_engine Qt6::Widgets Qt6::PrintSupport)
target_compile_definitions(cad4all PRIVATE
    CAD4ALL_VERSION="${PROJECT_VERSION}"
    CAD4ALL_AUTHOR="${CAD4ALL_AUTHOR}"
    CAD4ALL_EMAIL="${CAD4ALL_EMAIL}"
    CAD4ALL_YEARS="${CAD4ALL_YEARS}"
    CAD4ALL_HOMEPAGE="${PROJECT_HOMEPAGE_URL}")

# The licence text, shown by Aiuto > Licenza.
qt_add_resources(cad4all licence
    PREFIX /
    BASE ${PROJECT_SOURCE_DIR}
    FILES ${PROJECT_SOURCE_DIR}/LICENSE)

# The icon of the windows; resources/cad4all.svg is its source.
qt_add_resources(cad4all icons
    PREFIX /
    FILES
        resources/cad4all-16.png resources/cad4all-24.png resources/cad4all-32.png resources/cad4all-48.png
        resources/cad4all-64.png resources/cad4all-128.png resources/cad4all-256.png resources/cad4all-512.png)

# The icon of the program file: the Windows .rc below and the macOS bundle.
if(APPLE)
    set(macos_icon ${CMAKE_CURRENT_SOURCE_DIR}/resources/cad4all.icns)
    set_source_files_properties(${macos_icon} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(cad4all PRIVATE ${macos_icon})
endif()

if(WIN32)
    configure_file(cad4all.rc.in ${CMAKE_CURRENT_BINARY_DIR}/cad4all.rc @ONLY)
    target_sources(cad4all PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/cad4all.rc)
endif()

set_target_properties(cad4all PROPERTIES
    OUTPUT_NAME CAD4All
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_GUI_IDENTIFIER org.cad4all.CAD4All
    MACOSX_BUNDLE_BUNDLE_NAME CAD4All
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in
    MACOSX_BUNDLE_ICON_FILE cad4all.icns
    MACOSX_BUNDLE_COPYRIGHT "Copyright © ${CAD4ALL_YEARS} ${CAD4ALL_AUTHOR}"
)

include(GNUInstallDirs)
install(TARGETS cad4all
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Linux desktops: the menu entry, the icon in every size and the .c4a type.
if(UNIX AND NOT APPLE)
    set(packaging ${PROJECT_SOURCE_DIR}/packaging/linux)
    install(FILES ${packaging}/cad4all.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${packaging}/cad4all-mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages RENAME cad4all.xml)
    foreach(size 16 24 32 48 64 128 256 512)
        install(FILES resources/cad4all-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps RENAME cad4all.png)
    endforeach()
    install(FILES resources/cad4all.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

# The Windows installer script, compiled by Inno Setup after windeployqt.
if(WIN32)
    configure_file(${PROJECT_SOURCE_DIR}/packaging/windows/cad4all.iss.in ${PROJECT_BINARY_DIR}/packaging/cad4all.iss @ONLY)
endif()
