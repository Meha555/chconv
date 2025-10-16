cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(LINKING_STATIC "Linking static" ON)

project(chconv VERSION 1.1.0)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/out)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/out)

# 全局设置 CMP0077 默认值为 NEW，确保FetchContent的子项目继承
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
# 启用 CMP0135 新行为：解压文件的时间戳设为解压时的当前时间
cmake_policy(SET CMP0135 NEW)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "Clang" OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

include(FetchContent)
# include(ExternalProject)

include(cmake/FindLibchardet.cmake) # Using uchardet
include(cmake/FindLibiconv.cmake)
include(cmake/FindCmdline.cmake)

# 配置version.h文件
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.h"
    @ONLY
)

add_executable(chconv main.cpp)
target_include_directories(chconv PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(chconv PRIVATE ${CHARDET_LIB} ${ICONV_LIB} cmdline)

add_custom_command(
    TARGET chconv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_CURRENT_BINARY_DIR}/test
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/test
    ${CMAKE_CURRENT_BINARY_DIR}/test
)
