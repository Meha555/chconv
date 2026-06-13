include(FetchContent)
# include(ExternalProject)

include(FindLibchardet) # Using uchardet
include(FindLibiconv)
include(FindCmdline)
include(FindLibmagic)
include(FindIncbin)

if(EMBED_MAGIC_MGC_FILE)
    set(MAGIC_MGC_FILE ${PROJECT_SOURCE_DIR}/misc/magic.mgc)
endif()

# 配置version.h文件
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.h"
    @ONLY
)

find_package(Threads REQUIRED)

set(chconv_srcs
    main.cpp
    command/command.cpp
    command/convert.cpp
    command/guess.cpp
    converter.cpp
    encoding.cpp
)
set(chconv_libs ${CHARDET_LIB} ${ICONV_LIB} ${MAGIC_LIB} cmdline Threads::Threads)
if(LINUX)
    list(APPEND chconv_libs tbb)
endif()

add_executable(chconv ${chconv_srcs})
target_include_directories(chconv PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${INCBIN_INCLUDE_DIRS}
)
target_link_libraries(chconv PRIVATE ${chconv_libs})
target_compile_definitions(chconv PRIVATE MAGIC_MGC_FILE="${MAGIC_MGC_FILE}")
if(EMBED_MAGIC_MGC_FILE)
    target_compile_definitions(chconv PRIVATE EMBED_MAGIC_MGC_FILE)
    # 使用生成文件的add_custom_command形式，这种形式在所有CMake版本中都支持DEPENDS
    add_custom_command(
        # TARGET chconv PRE_BUILD
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/data.c
        COMMAND $<TARGET_FILE:incbin> ${CMAKE_CURRENT_SOURCE_DIR}/encoding.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/data.c
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
    ${PROJECT_SOURCE_DIR}/test
    ${CMAKE_CURRENT_BINARY_DIR}/test
)

# 安装配置
install(TARGETS chconv
    RUNTIME DESTINATION bin
)

# 安装依赖库
install(FILES
    ${PROJECT_SOURCE_DIR}/out/uchardet.dll
    ${PROJECT_SOURCE_DIR}/out/magic-1.dll
    ${PROJECT_SOURCE_DIR}/out/tre.dll
    DESTINATION bin
)

# 安装magic.mgc文件
install(FILES
    ${PROJECT_SOURCE_DIR}/misc/magic.mgc
    DESTINATION bin
)

# 安装README文件
install(FILES
    ${PROJECT_SOURCE_DIR}/README.md
    ${PROJECT_SOURCE_DIR}/README_zh.md
    DESTINATION doc
)

# CPack 配置
set(CPACK_PACKAGE_NAME "chconv")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION "File encoding converter")
set(CPACK_PACKAGE_VENDOR "chconv")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "chconv")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")

include(PackNSIS)
include(CPack)
