# Core: the document, its annotations and how they are drawn, screen
# capture and settings. No Qt Quick, so the tests link it directly.
qt_add_library(guckste_core STATIC)
qt_add_qml_module(guckste_core
    URI Guckste.Core
    VERSION 1.0
    SOURCES
        core/Annotation.h core/Annotation.cpp
        core/AnnotationPainter.h core/AnnotationPainter.cpp
        core/Document.h core/Document.cpp
        core/LayerModel.h core/LayerModel.cpp
        core/FileNaming.h core/FileNaming.cpp
        core/ScreenCapture.h core/ScreenCapture.cpp
        core/SingleInstance.h core/SingleInstance.cpp
        core/Preferences.h core/Preferences.cpp
        core/ScreenRecorder.h core/ScreenRecorder.cpp
        core/StillKeeper.h core/StillKeeper.cpp
        core/Encoding.h core/Encoding.cpp
        core/VideoFrames.h core/VideoFrames.cpp
        core/VideoProbe.h core/VideoProbe.cpp
        core/VideoExporter.h core/VideoExporter.cpp
        core/Playback.h core/Playback.cpp
        core/CursorCapture.h core/CursorCapture.cpp
        core/KeyListener.h core/KeyListener.cpp
        core/KeyOverlay.h core/KeyOverlay.cpp
        core/WindowFinder.h core/WindowFinder.cpp
)
# The generated QML type registrations include headers by file name only.
target_include_directories(guckste_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core
)
target_link_libraries(guckste_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Multimedia)
if(TARGET Qt6::DBus)
    target_link_libraries(guckste_core PUBLIC Qt6::DBus)
    target_compile_definitions(guckste_core PUBLIC GUCKSTE_HAVE_DBUS)
endif()
# The pointer image, the keys pressed anywhere and the windows on screen:
# XFixes, XInput2 and the window tree on X11; Win32 and DWM on Windows;
# AppKit, an event tap and the window list on macOS.
if(WIN32)
    target_link_libraries(guckste_core PUBLIC user32 gdi32 dwmapi)
elseif(APPLE)
    target_sources(guckste_core PRIVATE
        core/CursorCapture_mac.mm
        core/KeyListener_mac.mm
        core/WindowFinder_mac.mm
    )
    target_link_libraries(guckste_core PUBLIC
        "-framework AppKit" "-framework Carbon" "-framework ApplicationServices" "-framework CoreGraphics")
elseif(X11_FOUND AND X11_Xfixes_FOUND AND X11_Xi_FOUND)
    target_link_libraries(guckste_core PUBLIC X11::X11 X11::Xfixes X11::Xi)
    target_compile_definitions(guckste_core PUBLIC GUCKSTE_HAVE_X11)
    # Grabbing the screens through shared memory, much faster than Qt.
    if(X11_Xext_FOUND)
        target_sources(guckste_core PRIVATE core/ScreenCapture_x11.cpp)
        target_link_libraries(guckste_core PUBLIC X11::Xext)
        target_compile_definitions(guckste_core PRIVATE GUCKSTE_HAVE_XSHM)
    endif()
endif()

# UI items and the application object, which the UI test loads too.
qt_add_library(guckste_ui STATIC)
qt_add_qml_module(guckste_ui
    URI Guckste.Ui
    VERSION 1.0
    SOURCES
        ui/AnnotationLayer.h ui/AnnotationLayer.cpp
        ui/Ruler.h ui/Ruler.cpp
        ui/DocumentImageProvider.h ui/DocumentImageProvider.cpp
        ui/App.h ui/App.cpp
        ui/Clipboard.h
)
target_include_directories(guckste_ui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui
)
target_link_libraries(guckste_ui PUBLIC
    guckste_core
    guckste_coreplugin
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
)

# The windows.
set_source_files_properties(qml/Icons.qml qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_library(guckste_qml STATIC)
qt_add_qml_module(guckste_qml
    URI Guckste
    VERSION 1.0
    QML_FILES
        qml/CaptureOverlay.qml
        qml/EditorWindow.qml
        qml/SettingsWindow.qml
        qml/RecordingControls.qml
        qml/ColorPicker.qml
        qml/Countdown.qml
        qml/DrawingArea.qml
        qml/LayerPanel.qml
        qml/Timeline.qml
        qml/TimelineLanes.qml
        qml/AnnotationToolbar.qml
        qml/TextEntry.qml
        qml/Icons.qml
        qml/Icon.qml
        qml/IconButton.qml
        qml/Tip.qml
        qml/Theme.qml
    RESOURCES
        icons/image-plus.svg
        icons/layers.svg
        icons/image.svg
        icons/chevron-up.svg
        icons/chevron-down.svg
        icons/trash-2.svg
        icons/lock.svg
        icons/lock-open.svg
        icons/camera.svg
        icons/clipboard-paste.svg
        icons/chevrons-right.svg
        icons/circle.svg
        icons/circle-stop.svg
        icons/copy.svg
        icons/download.svg
        icons/folder-open.svg
        icons/grid-2x2.svg
        icons/grid-3x3.svg
        icons/hash.svg
        icons/timer.svg
        icons/layout-grid.svg
        icons/history.svg
        icons/square-dashed.svg
        icons/app-window.svg
        icons/monitor.svg
        icons/keyboard.svg
        icons/highlighter.svg
        icons/maximize.svg
        icons/mouse-pointer-2.svg
        icons/move-up-right.svg
        icons/palette.svg
        icons/play.svg
        icons/pencil-ruler.svg
        icons/pause.svg
        icons/pencil.svg
        icons/redo-2.svg
        icons/ruler.svg
        icons/save.svg
        icons/skip-back.svg
        icons/skip-forward.svg
        icons/step-back.svg
        icons/step-forward.svg
        icons/settings.svg
        icons/slash.svg
        icons/square.svg
        icons/type.svg
        icons/undo-2.svg
        icons/video.svg
        icons/x.svg
        icons/film.svg
        icons/zoom-in.svg
        icons/zoom-out.svg
        logo/guckste.svg
)
target_link_libraries(guckste_qml PUBLIC guckste_ui guckste_uiplugin Qt6::Quick Qt6::QuickControls2 Qt6::QuickShapes)

qt_add_executable(guckste main.cpp)
target_compile_definitions(guckste PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(guckste PRIVATE guckste_qml guckste_qmlplugin)
# A window of its own on Windows, an app bundle on macOS, each with the
# icon file manager, Dock and taskbar show.
set_target_properties(guckste PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_ICON_FILE guckste.icns
)
if(WIN32)
    target_sources(guckste PRIVATE logo/guckste.rc)
elseif(APPLE)
    target_sources(guckste PRIVATE logo/guckste.icns)
    set_source_files_properties(logo/guckste.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

install(TARGETS guckste
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# Desktop environments find the icon by the desktop file's Icon= name.
install(FILES logo/guckste.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES logo/guckste.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
