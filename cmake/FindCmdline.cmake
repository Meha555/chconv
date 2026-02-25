FetchContent_Declare(
    cmdline
    GIT_REPOSITORY https://github.com/Meha555/cmdline.git
    GIT_TAG 1.0.0
    GIT_SHALLOW 1
)

option(BUILD_EXAMPLES "Build cmdline examples" OFF)

FetchContent_MakeAvailable(cmdline)