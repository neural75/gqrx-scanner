cmake_minimum_required(VERSION 2.8.12)

project(gqrx-scanner)
# Set the output folder where your program will be created
set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/bin)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE} CACHE PATH "Executable output path")
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE} CACHE PATH "Library output path")

# The following folder will be included
include_directories("${PROJECT_SOURCE_DIR}")
add_executable(gqrx-scanner ${PROJECT_SOURCE_DIR}/gqrx-scan.c ${PROJECT_SOURCE_DIR}/gqrx-prot.c)
target_link_libraries(gqrx-scanner m)
install (TARGETS gqrx-scanner DESTINATION bin)
# uninstall target
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
