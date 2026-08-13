cmake_minimum_required(VERSION 3.16)
project(recamera_depth_rtsp LANGUAGES CXX)

get_filename_component(ZOO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SKIP_BUILD_RPATH TRUE)

if(NOT DEFINED RECAMERA_RKNNRT OR NOT EXISTS "${RECAMERA_RKNNRT}")
  message(FATAL_ERROR "Set RECAMERA_RKNNRT to the local board-compatible librknnrt.so")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
  gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
set(RKNN_INCLUDE_DIR "${ZOO_ROOT}/3rdparty/rknn/include")
if(NOT EXISTS "${RKNN_INCLUDE_DIR}/rknn_api.h")
  message(FATAL_ERROR "Missing RKNN Toolkit2 2.3.2 rknn_api.h")
endif()
set(RTSP_SERVER_INCLUDE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/toolchain/rtsp-server-dev/usr/include/gstreamer-1.0")
find_library(RTSP_SERVER_LIBRARY gstrtspserver-1.0
  PATHS "${CMAKE_SYSROOT}/usr/lib" "${CMAKE_SYSROOT}/usr/lib64"
  NO_DEFAULT_PATH REQUIRED)

add_executable(recamera_depth_rtsp src/main.cpp)
target_compile_options(recamera_depth_rtsp PRIVATE -O3 -DNDEBUG -Wall -Wextra)
target_include_directories(recamera_depth_rtsp PRIVATE
  "${RKNN_INCLUDE_DIR}" "${RTSP_SERVER_INCLUDE_DIR}")
target_link_directories(recamera_depth_rtsp BEFORE PRIVATE
  "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu"
  "${CMAKE_SYSROOT}/usr/lib"
  "${CMAKE_SYSROOT}/lib")
target_link_libraries(recamera_depth_rtsp PRIVATE PkgConfig::GST
  "${RTSP_SERVER_LIBRARY}" "${RECAMERA_RKNNRT}"
  "${CMAKE_SYSROOT}/lib/ld-linux-aarch64.so.1")
target_link_options(recamera_depth_rtsp PRIVATE
  "-L${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu"
  "LINKER:-rpath,/userdata/rknn_test/3rdparty/lib")
