cmake_minimum_required(VERSION 2.6)

project(gqrx-scan)
# Set the output folder where your program will be created
set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/bin)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE} CACHE PATH "Executable output path")
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE} CACHE PATH "Library output path")

# The following folder will be included
include_directories("${PROJECT_SOURCE_DIR}")
add_executable(gqrx-scan ${PROJECT_SOURCE_DIR}/gqrx-scan.c ${PROJECT_SOURCE_DIR}/gqrx-prot.c)
target_link_libraries(gqrx-scan m)
install (TARGETS gqrx-scan DESTINATION bin)
