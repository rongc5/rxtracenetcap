# RapidJSON - Header-only library

# Collect all header files recursively
file(GLOB_RECURSE RAPIDJSON_HEADERS "*.h")

# Create interface library for header-only library
add_library(rapidjson INTERFACE)

target_include_directories(rapidjson INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Install headers maintaining directory structure
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
        DESTINATION include/rapidjson
        FILES_MATCHING PATTERN "*.h")