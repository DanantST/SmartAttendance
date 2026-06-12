set(COMPONENT_ADD_INCLUDEDIRS "include")

idf_component_register(SRCS "sqlite3.c" "esp32.c" "shox96_0_2.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       REQUIRES mbedtls
                       PRIV_REQUIRES console spiffs spi_flash esp_system esp_rom)

target_compile_definitions(${COMPONENT_LIB} PRIVATE 
    SQLITE_THREADSAFE=1
    SQLITE_OS_OTHER=1
    SQLITE_TEMP_STORE=2
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_OMIT_DEPRECATED
)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu99 -w)
