cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(chconv VERSION 1.3.0)

option(EMBED_MAGIC_MGC_FILE "Embed magic.mgc file database" ON)

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
include(cmake/FindLibmagic.cmake)
include(cmake/FindIncbin.cmake)

# 配置version.h文件
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.h"
    @ONLY
)

find_package(Threads REQUIRED)

set(chconv_srcs ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
set(chconv_libs ${CHARDET_LIB} ${ICONV_LIB} ${MAGIC_LIB} cmdline Threads::Threads)
if (LINUX)
    list(APPEND chconv_libs tbb)
endif()

add_executable(chconv ${chconv_srcs})
target_include_directories(chconv PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${INCBIN_INCLUDE_DIRS})
target_link_libraries(chconv PRIVATE ${chconv_libs})
target_compile_definitions(chconv PRIVATE MAGIC_MGC_FILE="${MAGIC_MGC_FILE}")
if(EMBED_MAGIC_MGC_FILE)
    target_compile_definitions(chconv PRIVATE EMBED_MAGIC_MGC_FILE)
    # 使用生成文件的add_custom_command形式，这种形式在所有CMake版本中都支持DEPENDS
    add_custom_command(
        # TARGET chconv PRE_BUILD
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/data.c
        COMMAND $<TARGET_FILE:incbin> ${chconv_srcs} -o ${CMAKE_CURRENT_BINARY_DIR}/data.c
        COMMENT "Embedding magic.mgc file database"
        DEPENDS incbin ${MAGIC_MGC_FILE}
    )
    # 标记data.c为生成文件，避免CMake在配置阶段检查文件是否存在
    # set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/data.c PROPERTIES GENERATED TRUE)
    # add_custom_target(incbin_data
    #     COMMAND $<TARGET_FILE:incbin> ${chconv_srcs} -o ${CMAKE_CURRENT_BINARY_DIR}/data.c
    #     COMMENT "Embedding magic.mgc file database"
    #     DEPENDS incbin
    # )
    # add_dependencies(chconv incbin_data)
    target_sources(chconv PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/data.c)
endif()
# if(EMBED_MAGIC_MGC_FILE)
#     file(READ ${MAGIC_MGC_FILE} MAGIC_DATA HEX)

#     # 将十六进制字符串转换为逗号分隔的十进制值
#     string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," MAGIC_HEX_LIST ${MAGIC_DATA})
#     string(REGEX REPLACE ",$" "" MAGIC_HEX_LIST ${MAGIC_HEX_LIST})

#     # 计算数据长度
#     string(LENGTH "${MAGIC_DATA}" HEX_LENGTH)
#     math(EXPR DATA_LENGTH "${HEX_LENGTH} / 2")

#     # 生成头文件内容
#     set(MAGIC_HEADER_CONTENT "static const unsigned char g_magic_database_buffer[${DATA_LENGTH}] = {${MAGIC_HEX_LIST}};")

#     # 写入头文件
#     file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/magic_static.h "#pragma once\n${MAGIC_HEADER_CONTENT}\n")
# endif()

add_custom_command(
    TARGET chconv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_CURRENT_BINARY_DIR}/test
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/test
    ${CMAKE_CURRENT_BINARY_DIR}/test
)
