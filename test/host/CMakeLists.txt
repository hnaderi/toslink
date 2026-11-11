cmake_minimum_required(VERSION 3.22)
project(toslink_host_test C)

include(FetchContent)
FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(unity)

set(COMPONENT_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)

add_executable(test_proto test_proto.c ${COMPONENT_DIR}/src/toslink_proto.c)
target_include_directories(test_proto PRIVATE ${COMPONENT_DIR}/include ${COMPONENT_DIR}/priv_include)
target_compile_options(test_proto PRIVATE -std=c11 -Wall -Wextra -Werror)
target_link_libraries(test_proto PRIVATE unity)

enable_testing()
add_test(NAME test_proto COMMAND test_proto)
