cmake_minimum_required(VERSION 3.16)

set(WEB_SRC "${CMAKE_CURRENT_SOURCE_DIR}/index.html")
set(WEB_GZ  "${CMAKE_CURRENT_BINARY_DIR}/index.html.gz")
set(WEB_H   "${CMAKE_CURRENT_BINARY_DIR}/web_content.h")

# Step 1: gzip the HTML
add_custom_command(
    OUTPUT "${WEB_GZ}"
    COMMAND gzip -9 -k -f -n "${WEB_SRC}" -c > "${WEB_GZ}"
    DEPENDS "${WEB_SRC}"
    COMMENT "Compressing web/index.html"
    VERBATIM
)

# Step 2: convert .gz binary to C header
add_custom_command(
    OUTPUT "${WEB_H}"
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/bin2h.py"
                    "${WEB_GZ}" "${WEB_H}"
                    web_content_gz
    DEPENDS "${WEB_GZ}"
    COMMENT "Generating web_content.h"
    VERBATIM
)

add_custom_target(web_content_header DEPENDS "${WEB_H}")
