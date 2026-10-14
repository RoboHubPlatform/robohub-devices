cmake_minimum_required(VERSION 3.16)
project(robohub-device-growatt-es C)

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
  foreach(level RANGE 6)
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
robohub_locate_module(robohub-modbus ROBOHUB_MODBUS_DIR)
if(NOT ROBOHUB_COMMON_DIR OR NOT ROBOHUB_MODBUS_DIR)
  message(FATAL_ERROR "robohub-common or robohub-modbus not found")
endif()

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT TARGET robohub-common)
    add_subdirectory("${ROBOHUB_COMMON_DIR}" robohub-common)
  endif()
  if(NOT TARGET robohub-modbus)
    add_subdirectory("${ROBOHUB_MODBUS_DIR}" robohub-modbus)
  endif()
endif()

add_library(robohub-device-growatt-es STATIC src/growatt.c)

set(ROBOHUB_GROWATT_NAMESPACE "robohub/devices/inverter")
set(ROBOHUB_GROWATT_INCLUDE_GEN "${CMAKE_CURRENT_BINARY_DIR}/include/${ROBOHUB_GROWATT_NAMESPACE}")
file(MAKE_DIRECTORY "${ROBOHUB_GROWATT_INCLUDE_GEN}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/inc/growatt.h"
               "${ROBOHUB_GROWATT_INCLUDE_GEN}/growatt.h" COPYONLY)

target_include_directories(robohub-device-growatt-es PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${ROBOHUB_COMMON_DIR}/include
)
target_link_libraries(robohub-device-growatt-es PUBLIC robohub-common robohub-modbus)
target_compile_features(robohub-device-growatt-es PUBLIC c_std_11)

install(FILES module.yml Kconfig DESTINATION share/robohub-device-growatt-es)
install(FILES inc/growatt.h DESTINATION include/robohub/devices/inverter)
install(TARGETS robohub-device-growatt-es ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
