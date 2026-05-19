add_library(Slang INTERFACE)

target_include_directories(Slang INTERFACE
    "$ENV{VULKAN_SDK}/Include"
)
target_link_directories(Slang INTERFACE
    "$ENV{VULKAN_SDK}/Lib"
)
target_link_libraries(Slang INTERFACE
    "slang"
)
