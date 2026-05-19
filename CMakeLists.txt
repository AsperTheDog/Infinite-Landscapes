cmake_minimum_required(VERSION 3.21)
project(InfiniteLandscapes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(sdl3_src
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.4
)
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG vulkan-sdk-1.3.296.0
)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
)

FetchContent_MakeAvailable(sdl3_src)
FetchContent_MakeAvailable(volk)
FetchContent_MakeAvailable(spdlog)

add_subdirectory(libs/SDL3)
add_subdirectory(libs/Slang)
add_subdirectory(libs/ImGui)
add_subdirectory(app)
