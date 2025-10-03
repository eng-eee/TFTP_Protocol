cmake_minimum_required(VERSION 3.22 FATAL_ERROR)


set(target unit_test)
find_package(GTest REQUIRED)

add_executable(${target} main.cc) 

target_link_libraries(${target} PRIVATE
    GTest::gtest
    GTest::gtest_main
    ${PROJECT_NAME}_implementation
)

file(GLOB_RECURSE HEADERS mock_files/*.h)
file(GLOB_RECURSE SOURCES test_files/*.cc)

target_sources(${target} PRIVATE ${SOURCES} ${HEADERS})


# Register the test
add_test(NAME RunAllTests COMMAND ${target})

add_dependencies(${target} ${PROJECT_NAME}_implementation)
