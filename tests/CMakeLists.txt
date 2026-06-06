cmake_minimum_required(VERSION 3.10.0)

# Find cmocka
find_package(cmocka REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/tests/mocks)
include_directories(${CMOCKA_INCLUDE_DIR})

# Unit test executable (utility, protocol, filter, file ops)
add_executable(unit_tests unit_tests.c
    ${CMAKE_SOURCE_DIR}/src/gqrx-scan.c
    ${CMAKE_SOURCE_DIR}/src/gqrx-prot.c)
target_compile_definitions(unit_tests PRIVATE TESTING_BUILD)

if(NOT APPLE)
    target_compile_definitions(unit_tests PRIVATE HAVE_WRAP_SOCKET_MOCKS)
    target_sources(unit_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/mocks/mock_socket.c
        ${CMAKE_SOURCE_DIR}/src/vox-audio.c
        ${CMAKE_SOURCE_DIR}/src/vox-lpc.c
        ${CMAKE_SOURCE_DIR}/src/vox-vad.c)
    target_link_libraries(unit_tests PRIVATE
        -Wl,--wrap=write -Wl,--wrap=read -Wl,--wrap=socket
        -Wl,--wrap=gethostbyname -Wl,--wrap=connect)
endif()

target_link_libraries(unit_tests PRIVATE ${CMOCKA_LIBRARY} m)

add_test(NAME unit_tests COMMAND unit_tests)
set_tests_properties(unit_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 30)

# Sweep integration test executable (Linux only — uses --wrap)
if(NOT APPLE)
    add_executable(sweep_test
        ${CMAKE_SOURCE_DIR}/tests/sweep_test.c
        ${CMAKE_SOURCE_DIR}/tests/sweep_parser.c
        ${CMAKE_SOURCE_DIR}/tests/sweep_mock.c
        ${CMAKE_SOURCE_DIR}/tests/mocks/mock_socket.c
        ${CMAKE_SOURCE_DIR}/src/gqrx-scan.c
        ${CMAKE_SOURCE_DIR}/src/gqrx-prot.c
        ${CMAKE_SOURCE_DIR}/src/vox-audio.c
        ${CMAKE_SOURCE_DIR}/src/vox-lpc.c
        ${CMAKE_SOURCE_DIR}/src/vox-vad.c)
    target_compile_definitions(sweep_test PRIVATE TESTING_BUILD)
    target_link_libraries(sweep_test PRIVATE
        ${CMOCKA_LIBRARY} m
        -Wl,--wrap=write -Wl,--wrap=read -Wl,--wrap=socket
        -Wl,--wrap=gethostbyname -Wl,--wrap=connect
        -Wl,--wrap=usleep -Wl,--wrap=select -Wl,--wrap=kbhit
        -Wl,--wrap=fgetc -Wl,--wrap=tcgetattr -Wl,--wrap=tcsetattr)
    add_test(NAME sweep_test COMMAND sweep_test)
    set_tests_properties(sweep_test PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 30)
endif()
