add_executable(InfiniteLandscapes)
target_link_libraries(InfiniteLandscapes PRIVATE SDL3)
target_link_libraries(InfiniteLandscapes PRIVATE ImGui)
target_link_libraries(InfiniteLandscapes PRIVATE Slang)
target_link_libraries(InfiniteLandscapes PRIVATE volk)
target_link_libraries(InfiniteLandscapes PRIVATE spdlog)
target_link_libraries(InfiniteLandscapes PRIVATE Threads::Threads)

file(GLOB InfiniteLandscapes_SOURCES_0 CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.hpp"
    "src/engine/*.cpp"
    "src/engine/*.hpp"
    "src/chunk_table/*.cpp"
    "src/chunk_table/*.hpp"
    "src/shaders/*.slang"
)
target_sources(InfiniteLandscapes PRIVATE ${InfiniteLandscapes_SOURCES_0})
target_include_directories(InfiniteLandscapes PRIVATE
    "src/"
    "$ENV{VULKAN_SDK}/Include"
)
target_compile_definitions(InfiniteLandscapes PRIVATE VK_NO_PROTOTYPES=1)
if(WIN32)
    add_custom_command(TARGET InfiniteLandscapes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$ENV{VULKAN_SDK}/Bin/slang.dll"
            $<TARGET_FILE_DIR:InfiniteLandscapes>
    )
endif()
if(MSVC)
    target_compile_options(InfiniteLandscapes PRIVATE
        "/W4"
    )
else()
    target_compile_options(InfiniteLandscapes PRIVATE
        "-Wall"
        "-Wextra"
    )
endif()
target_compile_definitions(InfiniteLandscapes PRIVATE $<$<CONFIG:Debug>:DEBUG=1>)

get_target_property(InfiniteLandscapes_ALL_SOURCES InfiniteLandscapes SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${InfiniteLandscapes_ALL_SOURCES})

add_custom_command(TARGET InfiniteLandscapes POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:InfiniteLandscapes>
        $<TARGET_FILE_DIR:InfiniteLandscapes>
    COMMAND_EXPAND_LISTS
)
