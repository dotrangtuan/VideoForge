cmake_minimum_required(VERSION 3.21)
project(VideoForgeEditor VERSION 0.10.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets)

# Widget-independent render/job core, shared by editor and integration tests.
add_library(VideoForgeRender STATIC
    src/BackgroundComposition.cpp src/BackgroundComposition.h src/ExportRange.h
    src/ExportQueue.cpp src/ExportQueue.h
    src/ProjectFile.cpp src/ProjectFile.h src/TimelineClip.h
    src/FFmpegExporter.cpp src/FFmpegExporter.h
    src/VideoEffects.cpp src/VideoEffects.h src/SequenceSettings.h)
target_include_directories(VideoForgeRender PUBLIC src)
target_link_libraries(VideoForgeRender PUBLIC Qt6::Core Qt6::Gui)
if(MSVC)
    target_compile_options(VideoForgeRender PRIVATE /utf-8)
    target_compile_definitions(VideoForgeRender PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

set(VIDEOFORGE_SOURCES
    src/EditingModes.cpp
    src/RealtimePreview.cpp
    src/RealtimePreview.h
    src/ProjectActions.cpp
    src/AudioEnvelope.h
    src/EffectsPanel.h
    src/EffectsPanel.cpp
    src/SequenceSettings.h
    src/SequenceActions.cpp
    src/QueueActions.cpp
    src/ExportQueueDialog.cpp
    src/ExportQueueDialog.h
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/MediaListWidget.h
    src/MediaListWidget.cpp
    src/TimelineWidget.h
    src/TimelineRuler.h
    src/TimelineWidget.cpp
)

if(WIN32)
    qt_add_executable(VideoForgeEditor WIN32 ${VIDEOFORGE_SOURCES})
else()
    qt_add_executable(VideoForgeEditor ${VIDEOFORGE_SOURCES})
endif()

target_link_libraries(VideoForgeEditor PRIVATE
    VideoForgeRender
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)

if(WIN32)
    target_compile_definitions(VideoForgeEditor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(APPLE)
    set_target_properties(VideoForgeEditor PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "VideoForgeEditor"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.videoforge.editor"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

# A basic install target. For a fully portable Windows folder, use
# scripts/build_windows.ps1, which also runs windeployqt and copies FFmpeg.
install(TARGETS VideoForgeEditor
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)


if(MSVC)
    target_compile_options(VideoForgeEditor PRIVATE /utf-8)
endif()
option(BUILD_TESTING "Build integration tests (requires Qt Test and FFmpeg)" OFF)
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(VideoForgeTests tests/EditorTests.cpp src/EditingModes.cpp src/QueueActions.cpp src/ExportQueueDialog.cpp src/ExportQueueDialog.h src/EffectsPanel.cpp src/EffectsPanel.h src/SequenceActions.cpp src/ProjectActions.cpp src/RealtimePreview.cpp src/RealtimePreview.h src/MainWindow.cpp src/MainWindow.h src/MediaListWidget.cpp src/MediaListWidget.h src/TimelineWidget.cpp src/TimelineWidget.h)
    target_include_directories(VideoForgeTests PRIVATE src)
    target_link_libraries(VideoForgeTests PRIVATE VideoForgeRender Qt6::Widgets Qt6::Test Qt6::Multimedia Qt6::MultimediaWidgets)
    if(MSVC)
        target_compile_options(VideoForgeTests PRIVATE /utf-8)
        target_compile_definitions(VideoForgeTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    add_test(NAME VideoForgeTests COMMAND VideoForgeTests -o test-results.txt,txt)
    set_tests_properties(VideoForgeTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

