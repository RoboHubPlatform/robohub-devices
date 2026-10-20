cmake_minimum_required(VERSION 3.16)
project(robohub-device-node C)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps.cmake")
  include("${CMAKE_CURRENT_SOURCE_DIR}/deps.cmake")
endif()

function(robohub_locate_module module out_var)
  string(TOUPPER "${module}" upper)
  string(REPLACE "-" "_" upper "${upper}")
  if(${out_var})
    return()
  endif()
  if(DEFINED ENV{${upper}_DIR})
    set(${out_var} "$ENV{${upper}_DIR}" PARENT_SCOPE)
    return()
  endif()
  set(dir "${CMAKE_CURRENT_SOURCE_DIR}")
  foreach(level RANGE 8)
    get_filename_component(parent "${dir}/.." ABSOLUTE)
    if(EXISTS "${parent}/${module}/CMakeLists.txt")
      set(${out_var} "${parent}/${module}" PARENT_SCOPE)
      return()
    endif()
    set(dir "${parent}")
  endforeach()
  include(FetchContent)
  message(STATUS "[${PROJECT_NAME}] fetching ${module}")
  FetchContent_Declare(${module}
    GIT_REPOSITORY "https://github.com/RoboHubPlatform/${module}.git"
    GIT_TAG main GIT_SHALLOW TRUE GIT_PROGRESS TRUE)
  FetchContent_MakeAvailable(${module})
  set(${out_var} "${${module}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

robohub_locate_module(robohub-common ROBOHUB_COMMON_DIR)
robohub_locate_module(robohub-core ROBOHUB_CORE_DIR)
robohub_locate_module(robohub-transport ROBOHUB_TRANSPORT_DIR)
robohub_locate_module(robohub-telemetry ROBOHUB_TELEMETRY_DIR)
if(NOT ROBOHUB_COMMON_DIR OR NOT ROBOHUB_CORE_DIR OR NOT ROBOHUB_TRANSPORT_DIR OR NOT ROBOHUB_TELEMETRY_DIR)
  message(FATAL_ERROR "robohub-device-node needs robohub-common, robohub-core, robohub-transport and robohub-telemetry")
endif()

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  foreach(dep robohub-common robohub-core robohub-transport robohub-telemetry)
    if(NOT TARGET ${dep})
      add_subdirectory("${${dep}_DIR}" ${dep})
    endif()
  endforeach()
endif()

add_library(robohub-device-node STATIC
    src/device_node.c
    src/protocol/protocol.c
)

set(ROBOHUB_DEVICE_NODE_NAMESPACE "robohub/device-node")
set(ROBOHUB_DEVICE_NODE_INCLUDE_GEN "${CMAKE_CURRENT_BINARY_DIR}/include/${ROBOHUB_DEVICE_NODE_NAMESPACE}")
file(MAKE_DIRECTORY "${ROBOHUB_DEVICE_NODE_INCLUDE_GEN}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/inc/device_node.h"
               "${ROBOHUB_DEVICE_NODE_INCLUDE_GEN}/device_node.h" COPYONLY)

target_include_directories(robohub-device-node PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol
    ${ROBOHUB_COMMON_DIR}/include
    ${ROBOHUB_CORE_DIR}/include
    ${ROBOHUB_TRANSPORT_DIR}/include
    ${ROBOHUB_TELEMETRY_DIR}/include
)
target_link_libraries(robohub-device-node PUBLIC robohub-common robohub-core robohub-transport robohub-telemetry)
target_compile_features(robohub-device-node PUBLIC c_std_11)

install(FILES module.yml Kconfig DESTINATION share/robohub-device-node)
install(FILES inc/device_node.h DESTINATION include/robohub/device-node)
install(TARGETS robohub-device-node ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
