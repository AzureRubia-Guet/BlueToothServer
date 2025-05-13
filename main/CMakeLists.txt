idf_component_register(SRCS "gatts_table_creat_demo.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi
                    REQUIRES nvs_flash
                    REQUIRES bt 
                    REQUIRES messageHandle
                    )
