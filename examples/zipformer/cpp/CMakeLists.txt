cmake_minimum_required(VERSION 3.16)
project(recamera_stt VERSION 0.1.0 LANGUAGES C CXX)

get_filename_component(ZOO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    message(FATAL_ERROR "RKNN 版本只能为 reCamera Pro (aarch64) 交叉构建")
endif()

set(RKNNRT_LIBRARY "${RECAMERA_RKNNRT}" CACHE FILEPATH "Target librknnrt.so used at link time")
if(RKNNRT_LIBRARY STREQUAL "")
    set(RKNNRT_LIBRARY "$ENV{RECAMERA_RKNNRT}")
endif()
if(RKNNRT_LIBRARY STREQUAL "")
    set(RKNNRT_LIBRARY "${CMAKE_SYSROOT}/usr/lib/librknnrt.so")
endif()
if(NOT EXISTS "${RKNNRT_LIBRARY}")
    message(FATAL_ERROR "找不到目标板 librknnrt.so；请设置 RECAMERA_RKNNRT")
endif()
add_library(rknnrt SHARED IMPORTED)
set_target_properties(rknnrt PROPERTIES IMPORTED_LOCATION "${RKNNRT_LIBRARY}")

find_package(Threads REQUIRED)

add_executable(recamera_stt src/main.cpp src/rknn_zipformer.cpp)
target_include_directories(recamera_stt PRIVATE
    "${ZOO_ROOT}/3rdparty/rknn/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/include")
target_link_libraries(recamera_stt PRIVATE
    rknnrt
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libkaldi-native-fbank-core.a"
    Threads::Threads m)
set_target_properties(recamera_stt PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "/userdata/rknn_test/3rdparty/lib")
target_compile_options(recamera_stt PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-fno-pie>)
target_link_options(recamera_stt PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-no-pie>)
install(TARGETS recamera_stt RUNTIME DESTINATION bin)
