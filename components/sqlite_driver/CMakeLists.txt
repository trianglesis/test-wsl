idf_component_register(
                SRCS 
                                "sqlite_driver.c"
                REQUIRES
                                vfs esp_timer sqlite3 json
                INCLUDE_DIRS 
                                "include"
)
