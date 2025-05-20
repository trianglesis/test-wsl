idf_component_register(
    SRCS 
                "littlefs_driver.c"
    REQUIRES
                littlefs
    PRIV_REQUIRES
                driver mbedtls
    INCLUDE_DIRS 
                "include"
)
