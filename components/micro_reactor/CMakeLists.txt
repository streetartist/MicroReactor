# MicroReactor (FluxOne) v3.0 ESP-IDF Component
# Zero-allocation reactive embedded framework

idf_component_register(
    SRCS
        "src/ur_core.c"
        "src/ur_pipe.c"
        "src/extensions/ur_transducers.c"
        "src/extensions/ur_wormhole.c"
        "src/extensions/ur_supervisor.c"
        "src/extensions/ur_panic.c"
        "src/extensions/ur_bus.c"
        "src/extensions/ur_param.c"
        "src/extensions/ur_codec.c"
        "src/extensions/ur_power.c"
        "src/extensions/ur_trace.c"
        "src/extensions/ur_acl.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        freertos
        driver
        esp_timer
    PRIV_REQUIRES
        esp_ringbuf
        nvs_flash
        esp_hw_support
        esp_pm
)

# Optional: Enable extra warnings for development
if(CONFIG_UR_ENABLE_EXTRA_WARNINGS)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wpedantic)
endif()
