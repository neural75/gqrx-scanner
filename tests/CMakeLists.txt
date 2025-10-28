cmake_minimum_required(VERSION 3.5.0)

# Find cmocka
find_package(cmocka REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/tests/mocks)
include_directories(${CMOCKA_INCLUDE_DIR})

# Consolidated test executable
add_executable(all_tests all_tests.c ${CMAKE_SOURCE_DIR}/gqrx-scan.c ${CMAKE_SOURCE_DIR}/gqrx-prot.c)
target_compile_definitions(all_tests PRIVATE TESTING_BUILD)
target_link_libraries(all_tests ${CMOCKA_LIBRARY} m)

# Add consolidated test to CTest
add_test(NAME all_tests COMMAND all_tests)

# Set working directory for tests
set_tests_properties(all_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
