cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(DataBaseApp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
endif()

# Main executable
add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cc)

# Compile options
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# Output binary dir
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PROJECT_NAME}/${CMAKE_BUILD_TYPE}/bin
)

# Install rule
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

# Add implementation lib from src/
add_subdirectory(src)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_implementation)

# Add tests only for non-Release builds
if(${CMAKE_BUILD_TYPE} STREQUAL "Release")
    message(STATUS "Including tests for non-Release build.")
    enable_testing()
    add_subdirectory(test)
else()
    message(STATUS "Skipped utest for non-Release build.")
    # enable_testing()
    # add_subdirectory(test)
endif()