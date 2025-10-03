# Build a library with the implementation
set(target ${PROJECT_NAME}_implementation)

file(GLOB SRC_FILES *.cc)
file(GLOB HDR_FILES *.h)

add_subdirectory(StorageLayer)

list(REMOVE_ITEM SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/main.cc) # exclude main

add_library(${target} ${SRC_FILES})
target_sources(${target} PRIVATE ${HDR_FILES})

target_link_libraries(${target} PRIVATE StorageLayer)
