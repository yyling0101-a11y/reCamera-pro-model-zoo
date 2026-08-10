cmake_minimum_required(VERSION 3.16)
project(recamera_tts_benchmark LANGUAGES CXX)
get_filename_component(ZOO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT DEFINED RECAMERA_RKNNRT OR NOT EXISTS "${RECAMERA_RKNNRT}")
  message(FATAL_ERROR "Set RECAMERA_RKNNRT to the board-compatible librknnrt.so")
endif()
set(RKNN_INCLUDE_DIR "${ZOO_ROOT}/3rdparty/rknn/include")
if(NOT EXISTS "${RKNN_INCLUDE_DIR}/rknn_api.h")
  message(FATAL_ERROR "Missing RKNN Toolkit2 2.3.2 rknn_api.h")
endif()
add_executable(recamera_tts_benchmark
  src/audio.cpp
  src/config.cpp
  src/http.cpp
  src/main.cpp
  src/tts_engine.cpp)
target_include_directories(recamera_tts_benchmark PRIVATE "${RKNN_INCLUDE_DIR}")
target_compile_options(recamera_tts_benchmark PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(recamera_tts_benchmark PRIVATE "${RECAMERA_RKNNRT}")
set_target_properties(recamera_tts_benchmark PROPERTIES
  BUILD_WITH_INSTALL_RPATH TRUE
  INSTALL_RPATH "/userdata/rknn_test/3rdparty/lib")
install(TARGETS recamera_tts_benchmark DESTINATION .)
