FetchContent_Declare(
    incbin
    GIT_REPOSITORY https://github.com/graphitemaster/incbin.git
    GIT_TAG main
    GIT_SHALLOW 1
)
FetchContent_MakeAvailable(incbin)

set(INCBIN_INCLUDE_DIRS ${incbin_SOURCE_DIR})

if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "Clang" OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    add_executable(incbin ${INCBIN_INCLUDE_DIRS}/incbin.c)
    target_include_directories(incbin PRIVATE ${INCBIN_INCLUDE_DIRS})
endif()