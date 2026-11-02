add_library(robohub-mqtt STATIC src/mqtt.c)
target_include_directories(robohub-mqtt PUBLIC inc)
target_link_libraries(robohub-mqtt PUBLIC robohub-common)
