cmake_minimum_required(VERSION 3.0)

project(cuda-rtsp-server)

option(CUDA_RTSP_EXAMPLE "Build example program" OFF)

find_package(PkgConfig REQUIRED)
pkg_search_module(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)
pkg_search_module(GSTREAMER-APP REQUIRED IMPORTED_TARGET gstreamer-app-1.0)
pkg_search_module(GSTREAMER-CUDA REQUIRED IMPORTED_TARGET gstreamer-cuda-1.0)
pkg_search_module(GSTREAMER-RTSP REQUIRED IMPORTED_TARGET gstreamer-rtsp-1.0)
pkg_search_module(GSTREAMER-RTSP-SERVER REQUIRED IMPORTED_TARGET gstreamer-rtsp-server-1.0)
pkg_search_module(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_search_module(CUDA REQUIRED IMPORTED_TARGET cuda)


add_library(cudartsp SHARED cuda_rtsp.c)

target_include_directories(cudartsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    cudartsp
    PUBLIC 
    PkgConfig::GSTREAMER 
    PkgConfig::GSTREAMER-APP
    PkgConfig::GSTREAMER-CUDA
    PkgConfig::GSTREAMER-RTSP
    PkgConfig::GSTREAMER-RTSP-SERVER
    PkgConfig::GLIB
    PkgConfig::CUDA
    )

if(${CUDA_RTSP_EXAMPLE})
    add_subdirectory(example)
endif()