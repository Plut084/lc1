# Download VMA-Hpp release
include(FetchContent)
FetchContent_Declare(
    vmahpp
    URL      https://github.com/YaaZ/VulkanMemoryAllocator-Hpp/releases/download/v3.4.0%2B2/VulkanMemoryAllocator-Hpp-3.4.0.tar.gz
    URL_HASH SHA256=588e4aa20898dedca99e72d1bfa428303d3076102108884b8b6a178195404bc9 # Copy from the asset list on the release page
    SYSTEM
)

