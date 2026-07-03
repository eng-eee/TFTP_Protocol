cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(TFTP_Protocol VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LOG4CXX REQUIRED IMPORTED_TARGET liblog4cxx)

add_executable(${PROJECT_NAME} main.cc)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        PkgConfig::LOG4CXX
        Threads::Threads
)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)