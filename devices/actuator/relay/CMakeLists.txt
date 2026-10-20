cmake_minimum_required(VERSION 3.16)
project(robohub-device-relay C)

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
if(NOT ROBOHUB_COMMON_DIR)
  message(FATAL_ERROR "robohub-common not found")
endif()

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT TARGET robohub-common)
    add_subdirectory("${ROBOHUB_COMMON_DIR}" robohub-common)
  endif()
endif()

add_library(robohub-device-relay STATIC src/relay.c)

set(ROBOHUB_RELAY_NAMESPACE "robohub/devices/actuator")
set(ROBOHUB_RELAY_INCLUDE_GEN "${CMAKE_CURRENT_BINARY_DIR}/include/${ROBOHUB_RELAY_NAMESPACE}")
file(MAKE_DIRECTORY "${ROBOHUB_RELAY_INCLUDE_GEN}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/inc/relay.h"
               "${ROBOHUB_RELAY_INCLUDE_GEN}/relay.h" COPYONLY)

target_include_directories(robohub-device-relay PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${ROBOHUB_COMMON_DIR}/include
)
target_link_libraries(robohub-device-relay PUBLIC robohub-common)
target_compile_features(robohub-device-relay PUBLIC c_std_11)

install(FILES module.yml Kconfig DESTINATION share/robohub-device-relay)
install(FILES inc/relay.h DESTINATION include/robohub/devices/actuator)
install(TARGETS robohub-device-relay ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
