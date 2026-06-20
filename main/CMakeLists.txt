idf_component_register(
    SRCS
        "main.c"
        "encoder.c"
        "ux_task.c"
        "param_store.c"
        "ws_server.c"
        "http_server.c"
        "wifi_ap.c"
        "dns_server.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_http_server
        esp_timer
        mdns
        freertos
        driver
        esp_driver_gpio
        lwip
)

# The web build target places web_content.h in the build tree.
# Add the build directory of the web subdirectory as an include path.
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_BINARY_DIR}/web"
)

add_dependencies(${COMPONENT_LIB} web_content_header)
