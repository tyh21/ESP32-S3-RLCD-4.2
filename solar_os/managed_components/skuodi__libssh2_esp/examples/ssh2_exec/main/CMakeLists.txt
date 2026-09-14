idf_component_register(SRCS "ssh2_exec.c"
                       INCLUDE_DIRS ".")

idf_build_get_property(use_pubkey CONFIG_EXAMPLE_SSH_USE_PUBKEY)

if(use_pubkey)

idf_build_get_property(PRIVKEY_FILENAME CONFIG_EXAMPLE_SSH_PRIVKEY_FILE)

execute_process(COMMAND ssh-keygen -t rsa -b 4096 -m PEM -f ${PRIVKEY_FILENAME} -C "" -N ""
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/data)

endif()

littlefs_create_partition_image(storage ../data FLASH_IN_PROJECT)
