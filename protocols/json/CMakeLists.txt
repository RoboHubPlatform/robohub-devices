cmake_minimum_required(VERSION 3.16)
project(robohub-json C)

add_library(robohub-json STATIC src/json.c)

set(ROBOHUB_JSON_NAMESPACE "robohub/protocols")
set(ROBOHUB_JSON_INCLUDE_GEN "${CMAKE_CURRENT_BINARY_DIR}/include/${ROBOHUB_JSON_NAMESPACE}")
file(MAKE_DIRECTORY "${ROBOHUB_JSON_INCLUDE_GEN}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/inc/json.h"
               "${ROBOHUB_JSON_INCLUDE_GEN}/json.h" COPYONLY)

target_include_directories(robohub-json PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)
target_compile_features(robohub-json PUBLIC c_std_11)

install(FILES module.yml Kconfig DESTINATION share/robohub-json)
install(FILES inc/json.h DESTINATION include/robohub/protocols)
install(TARGETS robohub-json ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
