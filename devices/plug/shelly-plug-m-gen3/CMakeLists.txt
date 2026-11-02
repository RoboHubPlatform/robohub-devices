add_library(robohub-device-shelly-plug-m-gen3 STATIC src/shelly_plug.c)
target_include_directories(robohub-device-shelly-plug-m-gen3 PUBLIC inc)
target_link_libraries(robohub-device-shelly-plug-m-gen3 PUBLIC
    robohub-common robohub-http robohub-json robohub-mqtt)
