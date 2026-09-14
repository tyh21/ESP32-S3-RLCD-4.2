file(GLOB MICROPYTHON_EMBED_SRCS
    "micropython_embed/py/*.c"
    "micropython_embed/extmod/*.c"
    "micropython_embed/shared/runtime/*.c"
    "micropython_embed/port/*.c"
)

idf_component_register(
    SRCS ${MICROPYTHON_EMBED_SRCS}
    INCLUDE_DIRS
        "."
        "micropython_embed"
        "micropython_embed/port"
    PRIV_REQUIRES esp_hw_support
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
)
