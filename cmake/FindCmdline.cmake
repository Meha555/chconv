FetchContent_Declare(
    cmdline
    GIT_REPOSITORY https://github.com/Meha555/cmdline.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

option(BUILD_EXAMPLES "Build cmdline examples" OFF)

FetchContent_MakeAvailable(cmdline)