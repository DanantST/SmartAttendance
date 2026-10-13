import sys

def patch_memory_layout():
    p = r"C:\Espressif\frameworks\esp-idf-v5.4.2\components\heap\port\esp32p4\memory_layout.c"
    with open(p, "r", encoding="utf-8") as f:
        content = f.read()
    
    old_line = '[SOC_MEMORY_TYPE_TCM]    = { "TCM",     { MALLOC_CAP_TCM,                              ESP32P4_MEM_COMMON_CAPS | MALLOC_CAP_INTERNAL, 0 }},'
    new_line = '[SOC_MEMORY_TYPE_TCM]    = { "TCM",     { MALLOC_CAP_TCM,                              0, 0 }},'
    
    if old_line in content:
        with open(p, "w", encoding="utf-8") as f:
            f.write(content.replace(old_line, new_line))
        print("Patched memory_layout.c")
    else:
        print("memory_layout.c already patched or pattern not found")

def patch_esp_memory_utils_h():
    p = r"C:\Espressif\frameworks\esp-idf-v5.4.2\components\esp_hw_support\include\esp_memory_utils.h"
    with open(p, "r", encoding="utf-8") as f:
        content = f.read()
    
    old_code = """#if SOC_RTC_SLOW_MEM_SUPPORTED
    r |= ((intptr_t)p >= SOC_RTC_DATA_LOW && (intptr_t)p < SOC_RTC_DATA_HIGH);
#endif"""

    new_code = """#if SOC_RTC_SLOW_MEM_SUPPORTED
    r |= ((intptr_t)p >= SOC_RTC_DATA_LOW && (intptr_t)p < SOC_RTC_DATA_HIGH);
#endif
#if SOC_MEM_TCM_SUPPORTED
    r |= esp_ptr_in_tcm(p);
#endif"""

    if "#if SOC_MEM_TCM_SUPPORTED\n    r |= esp_ptr_in_tcm(p);" not in content:
        if old_code in content:
            with open(p, "w", encoding="utf-8") as f:
                f.write(content.replace(old_code, new_code))
            print("Patched esp_memory_utils.h")
        else:
            print("esp_memory_utils.h old_code not found")
    else:
        print("esp_memory_utils.h already patched")

def patch_esp_memory_utils_c():
    p = r"C:\Espressif\frameworks\esp-idf-v5.4.2\components\esp_hw_support\esp_memory_utils.c"
    with open(p, "r", encoding="utf-8") as f:
        content = f.read()

    old_code = """#if CONFIG_SPIRAM
    r |= esp_psram_check_ptr_addr(p);
#endif"""

    new_code = """#if CONFIG_SPIRAM
    r |= esp_psram_check_ptr_addr(p);
#endif
#if SOC_MEM_TCM_SUPPORTED
    r |= (ip >= SOC_TCM_LOW && ip < SOC_TCM_HIGH);
#endif"""

    if "(ip >= SOC_TCM_LOW && ip < SOC_TCM_HIGH)" not in content:
        if old_code in content:
            with open(p, "w", encoding="utf-8") as f:
                f.write(content.replace(old_code, new_code))
            print("Patched esp_memory_utils.c")
        else:
            print("esp_memory_utils.c old_code not found")
    else:
        print("esp_memory_utils.c already patched")

if __name__ == "__main__":
    patch_memory_layout()
    patch_esp_memory_utils_h()
    patch_esp_memory_utils_c()
