cmake_minimum_required(VERSION 3.22)
project(toslink_host_test C)

include(FetchContent)
FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(unity)

set(COMPONENT_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
enable_testing()

# One executable per protocol configuration.
function(toslink_test name)
    add_executable(${name} test_proto.c ${COMPONENT_DIR}/src/toslink_proto.c)
    target_include_directories(${name} PRIVATE ${COMPONENT_DIR}/include ${COMPONENT_DIR}/priv_include)
    target_compile_definitions(${name} PRIVATE ${ARGN})
    target_compile_options(${name} PRIVATE -std=c11 -Wall -Wextra -Werror)
    target_link_libraries(${name} PRIVATE unity)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

toslink_test(test_proto)
toslink_test(test_proto_max_payload CONFIG_TOSLINK_MAX_PAYLOAD=255)
