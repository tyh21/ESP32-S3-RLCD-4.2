idf_component_register(
    SRCS "user_app.cpp" "weather_client.c" "font_alipuhui20.c"
    PRIV_REQUIRES main ui_bsp
    REQUIRES esp_http_client
    INCLUDE_DIRS "./")
