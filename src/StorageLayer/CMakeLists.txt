cmake_minimum_required(VERSION 3.22 FATAL_ERROR)


set(target StorageLayer)


add_library(${target} OBJECT) 

file(GLOB_RECURSE HEADERS *.h)
file(GLOB_RECURSE SOURCES *.cc)

target_sources(${target} PRIVATE ${SOURCES} ${HEADERS})
