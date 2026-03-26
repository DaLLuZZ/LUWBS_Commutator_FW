#ifndef __NVS_SETTINGS_H_
#define __NVS_SETTINGS_H_

// configuration of non volatile storage for settings
#define CONFIG_NVS_SETTINGS_ADDR                            (0x081E0000)    // sector 23 on 2 Mbyte dual bank organization (STM32F42xxx and STM32F43xxx)
#define CONFIG_NVS_SETTINGS_SIZE                            (131072)        // sector 23 size (128K)
#define CONFIG_NVS_SETTINGS_MAGIC                           (0xA9)
#define CONFIG_NVS_SETTINGS_VERSION                         (0x00)          // 0xYZ: Y means major, Z minor
#define CONFIG_NVS_SETTINGS_VER_MAJOR_MASK                  (0xF0)
#define CONFIG_NVS_SETTINGS_VER_MINOR_MASK                  (0x0F)
#define CONFIG_NVS_SETTINGS_MAX_SETTING_SIZE_BYTES          (4)

// error flags for nvs_settings_validate_entry()
#define ERROR_NVS_SETTINGS_MAJ_VER_MISMATCH                 (1 << 0)        // major version mismatch of the stored entry and the current version of application expected value
#define ERROR_NVS_SETTINGS_MIN_VER_MIN_LESS                 (1 << 1)        // the minor version of the stored entry is lower than the current version of application expected
#define ERROR_NVS_SETTINGS_MIN_VER_MIN_GRTR                 (1 << 2)        // the minor version of the stored entry is greater than the current version of application expected
#define ERROR_NVS_SETTINGS_ENTRY_SIZE_LESS                  (1 << 3)        // the size of the stored entry is less than the current version of application expected
#define ERROR_NVS_SETTINGS_ENTRY_SIZE_GRTR                  (1 << 4)        // the size of the stored entry is greater than the current version of application expected
#define ERROR_NVS_SETTINGS_ENTRY_CRC_INVAL                  (1 << 5)        // invalid crc

#define NO_APPLY_FUNC NULL

// Flash API macroses

#define EEPROM_READ(src, dest, size)                        \
    memcpy((uint8_t*)dest, (uint8_t*)src, size)

#define EEPROM_WRITE(_dest, _src, _size)                    \
    do                                                      \
    {                                                       \
        uint32_t dest = (uint32_t)_dest;                    \
        uint8_t* src  = (uint8_t*)_src;                     \
        uint32_t size = (uint32_t)_size;                    \
        uint32_t type_program = FLASH_TYPEPROGRAM_BYTE;     \
        HAL_FLASH_Unlock();                                 \
        while (size)                                        \
        {                                                   \
            HAL_FLASH_Program(type_program, dest, *src);    \
            src++;                                          \
            dest++;                                         \
            size--;                                         \
        }                                                   \
        HAL_FLASH_Lock();                                   \
    } while (0)

// HACK on EEPROM_ERASE (fixed address for the defined bank)
// TODO: rework?
#define EEPROM_ERASE(addr, size)                            \
    do                                                      \
    {                                                       \
        if ((addr != CONFIG_NVS_SETTINGS_ADDR) ||           \
            (size != CONFIG_NVS_SETTINGS_SIZE))             \
        {                                                   \
            break;                                          \
        }                                                   \
        FLASH_EraseInitTypeDef erase_init;                  \
        uint32_t sector_error = 0;                          \
        erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;     \
        erase_init.Sector = FLASH_SECTOR_23;                \
        erase_init.NbSectors = 1;                           \
        erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;    \
        HAL_FLASH_Unlock();                                 \
        HAL_FLASHEx_Erase(&erase_init, &sector_error);      \
        HAL_FLASH_Lock();                                   \
    } while (0)

#define NVS_SETTING_GET(setting_name, value)                \
    nvs_settings_setting_get((uint8_t*)value, sizeof(((nvs_settings_t*)0)->setting_name), offsetof(nvs_settings_entry_t, settings.setting_name))

#define NVS_SETTING_SET(setting_name, value, apply_func)    \
    nvs_settings_setting_set((uint8_t*)value, sizeof(((nvs_settings_t*)0)->setting_name), offsetof(nvs_settings_entry_t, settings.setting_name), apply_func)

#define NVS_SETTINGS_IP_ADDR_ENCODE(b0, b1, b2, b3)         \
    ((nvs_settings_ip_addr_t){.byte = {b0, b1, b2, b3}}).value

#define NVS_SETTINGS_IP_MODE_ENCODE(param)                  \
    ((nvs_settings_ip_mode_t){.ip_mode = param}).value

typedef enum
{
    IP_MODE_AUTO = 0,
    IP_MODE_MANUAL,

    IP_MODE_COUNT
} _nvs_settings_ip_mode_t;

typedef union
{
    uint32_t value;
    uint8_t  byte[4];
    struct
    {
        uint8_t b0;
        uint8_t b1;
        uint8_t b2;
        uint8_t b3;
    } bytes;
} nvs_settings_ip_addr_t;

typedef union
{
    uint8_t value;
    _nvs_settings_ip_mode_t ip_mode;
} nvs_settings_ip_mode_t;

// do not pack this structure
// otherwise misaligned access can occur
typedef struct
{
    uint32_t crc32;
    uint16_t size;
    uint8_t  magic;
    uint8_t  version;
} nvs_settings_header_t;

// do not pack this structure
// otherwise misaligned access can occur
typedef struct
{
    uint32_t reserved32;
    uint32_t ip_addr;
    uint32_t subnet_mask;
    uint32_t gateway;

    uint16_t reserved16_1;
    uint16_t reserved16_2;

    uint8_t  ip_mode;

    // ...
} nvs_settings_t;

// do not pack this structure
// otherwise misaligned access can occur
typedef struct
{
    nvs_settings_header_t header;
    nvs_settings_t settings;
} nvs_settings_entry_t;

// (!!!) ATTENTION: for internal usage only!!! always use macro NVS_SETTING_GET instead of such a call!!!
void nvs_settings_setting_get(uint8_t* value, uint16_t size, uint16_t offset);
// (!!!) ATTENTION: for internal usage only!!! always use macro NVS_SETTING_SET instead of such a call!!!
void nvs_settings_setting_set(uint8_t* value, uint16_t size, uint16_t offset, void apply_func(uint8_t* value));

/******************************************************************************\
 * Apply functions for nvs settings
 * never call them directly, use only with MACROSes
 */
void nvs_settings_apply_ip_addr(uint8_t* ip_addr);
void nvs_settings_apply_subnet_mask(uint8_t* subnet_mask);
void nvs_settings_apply_gateway(uint8_t* gateway);
void nvs_settings_apply_ip_mode(uint8_t* ip_mode);

/******************************************************************************/

#endif // __NVS_SETTINGS_H_
