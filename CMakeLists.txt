cmake_minimum_required(VERSION 3.21)

project(guckste VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia Qml Quick QuickControls2)
# Wayland compositors only hand out screenshots through the desktop portal,
# which is a D-Bus service.
if(UNIX AND NOT APPLE)
    find_package(Qt6 COMPONENTS DBus)
    find_package(X11 COMPONENTS Xfixes Xi)
endif()
qt_standard_project_setup(REQUIRES 6.8)
# An installed guckste finds the Qt it was built against.
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

# Lay QML modules out by URI so qmllint and qmlls find them.
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)
    add_subdirectory(tests)
endif()
