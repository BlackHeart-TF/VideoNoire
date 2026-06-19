cmake_minimum_required(VERSION 3.21)

project(ClipThing VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-video-1.0
)

qt_standard_project_setup()

qt_add_executable(clipthing
    src/ClipItem.h
    src/GstPreviewEngine.cpp
    src/GstPreviewEngine.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/PreviewEngine.h
    src/SourceListWidget.cpp
    src/SourceListWidget.h
    src/TimelineWidget.cpp
    src/TimelineWidget.h
    src/VideoExporter.cpp
    src/VideoExporter.h
    src/main.cpp
)

target_link_libraries(clipthing
    PRIVATE
        Qt6::Widgets
        PkgConfig::GSTREAMER
)

install(TARGETS clipthing
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
