add_library(robohub-device-sonoff-s60tpf STATIC src/sonoff_s60tpf.c)
target_include_directories(robohub-device-sonoff-s60tpf PUBLIC inc)
target_link_libraries(robohub-device-sonoff-s60tpf PUBLIC
    robohub-common robohub-json robohub-mqtt)
