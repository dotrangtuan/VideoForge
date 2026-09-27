cmake_minimum_required(VERSION 3.21)
project(VideoForgeRenderBench LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
# Configure with -DVIDEOFORGE_SRC=/absolute/path/to/VideoForgeEditor/src.
if(NOT DEFINED VIDEOFORGE_SRC)
    get_filename_component(VIDEOFORGE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../src" ABSOLUTE)
endif()
add_executable(VideoForgeRenderBench RenderBench.cpp
    ${VIDEOFORGE_SRC}/FFmpegExporter.cpp ${VIDEOFORGE_SRC}/FFmpegExporter.h
    ${VIDEOFORGE_SRC}/VideoEffects.cpp)
target_include_directories(VideoForgeRenderBench PRIVATE ${VIDEOFORGE_SRC})
target_link_libraries(VideoForgeRenderBench PRIVATE Qt6::Core Qt6::Gui)
if(MSVC)
    target_compile_options(VideoForgeRenderBench PRIVATE /utf-8)
    target_compile_definitions(VideoForgeRenderBench PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
