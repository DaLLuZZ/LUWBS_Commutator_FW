#include "stm32f4xx_hal.h"
#include "nvs_settings.h"

static uint32_t nvs_settings_current_header_addr = 0xFFFFFFFF;

static uint32_t nvs_settings_calc_crc32(uint32_t crc, const uint8_t* buf, size_t len)
{
    int k;

    crc = ~crc;
    while (len--)
    {
        crc ^= *buf++;
        for (k = 0; k < 8; k++)
        {
            crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
        }
    }
    return ~crc;
}

// find the first address available for writing new entry
static uint32_t nvs_settings_find_empty_entry_addr()
{
    __attribute__((aligned(4))) nvs_settings_entry_t entry = {0xFF};

    uint32_t first_empty_entry_addr = 0xFFFFFFFF;
    // search for the first empty header in nvs
    for (uint32_t addr = CONFIG_NVS_SETTINGS_ADDR; addr < CONFIG_NVS_SETTINGS_ADDR + CONFIG_NVS_SETTINGS_SIZE - sizeof(entry); addr += sizeof(entry.header))
    {
        EEPROM_READ(addr, &entry, sizeof(entry));
        if (entry.header.magic == 0xFF)
        {
            uint8_t checked = 1;
            for (uint32_t i = 0; i < sizeof(entry); i++)
            {
                if (((uint8_t*)&entry)[i] != 0xFF)
                {
                    checked = 0;
                    break;
                }
            }
            if (checked)
            {
                DBG_PRINTF("Found free cfg entry at addr 0x%04X", addr);
                first_empty_entry_addr = addr;
                break;
            }
        }
    }

    if (first_empty_entry_addr == 0xFFFFFFFF)
    {
        DBG_PRINTF("No empty entry found");
    }

    return first_empty_entry_addr;
}

// entry pointer (argument 1) must be 4-byte aligned
static void nvs_settings_read_cfg(nvs_settings_entry_t* entry, uint32_t start_addr)
{
    EEPROM_READ(start_addr, entry, sizeof(entry->header));
    if (start_addr + entry->header.size <= CONFIG_NVS_SETTINGS_ADDR + CONFIG_NVS_SETTINGS_SIZE)
    {
        EEPROM_READ(start_addr, entry, entry->header.size);
    }
}

// entry pointer (argument 1) must be 4-byte aligned
static void nvs_settings_write_cfg(nvs_settings_entry_t* entry)
{
    entry->header.magic = CONFIG_NVS_SETTINGS_MAGIC;
    entry->header.version = CONFIG_NVS_SETTINGS_VERSION;
    entry->header.size = sizeof(nvs_settings_entry_t);
    entry->header.crc32 = nvs_settings_calc_crc32(0, (uint8_t*)&entry->settings, sizeof(entry->settings));

    uint32_t start_addr = nvs_settings_find_empty_entry_addr();
    if (start_addr >= CONFIG_NVS_SETTINGS_ADDR + CONFIG_NVS_SETTINGS_SIZE - sizeof(nvs_settings_entry_t))
    {
        // storage is about to overflow
        // need to erase it before writing
        DBG_PRINTF("Erasing non-volatile settings storage due to potential overflow");
        EEPROM_ERASE(CONFIG_NVS_SETTINGS_ADDR, CONFIG_NVS_SETTINGS_SIZE);
        start_addr = CONFIG_NVS_SETTINGS_ADDR;
    }

    EEPROM_WRITE(start_addr, (void*)entry, sizeof(nvs_settings_entry_t));
    if (nvs_settings_current_header_addr >= CONFIG_NVS_SETTINGS_ADDR && nvs_settings_current_header_addr < start_addr)
    {
        // invalidate old entry
        __attribute__((aligned(4))) nvs_settings_header_t header;
        EEPROM_READ(nvs_settings_current_header_addr, (uint8_t*)&header, sizeof(header));
        uint32_t inv_value = 0;
        for (uint32_t addr = nvs_settings_current_header_addr; addr <= nvs_settings_current_header_addr + header.size - sizeof(inv_value) && addr <= start_addr - sizeof(inv_value); addr += sizeof(inv_value))
        {
            EEPROM_WRITE(addr, (void*)&inv_value, sizeof(inv_value));
        }
    }
    nvs_settings_current_header_addr = start_addr;
}

static uint32_t nvs_settings_init_defaults(void)
{
    DBG_PRINTF("");

    // we need to init defaults (probably DataFlash region is clean, but we will clean it again to be sure)
    EEPROM_ERASE(CONFIG_NVS_SETTINGS_ADDR, CONFIG_NVS_SETTINGS_SIZE);

    __attribute__((aligned(4))) nvs_settings_entry_t entry = {0xFF};

    entry.settings.ip_mode = 0xFF;

    nvs_settings_write_cfg(&entry);
    return CONFIG_NVS_SETTINGS_ADDR;
}

// validates versioin, size, crc32
static uint8_t nvs_settings_validate_entry(nvs_settings_entry_t* entry)
{
    uint8_t ret = 0;

    if ((entry->header.version & CONFIG_NVS_SETTINGS_VER_MAJOR_MASK) != (CONFIG_NVS_SETTINGS_VERSION & CONFIG_NVS_SETTINGS_VER_MAJOR_MASK))
    {
        // major version mismatch of the stored entry and the current version of application expected value
        ret |= ERROR_NVS_SETTINGS_MAJ_VER_MISMATCH;
    }
    if ((entry->header.version & CONFIG_NVS_SETTINGS_VER_MINOR_MASK) < (CONFIG_NVS_SETTINGS_VERSION & CONFIG_NVS_SETTINGS_VER_MINOR_MASK))
    {
        // the minor version of the stored entry is lower than the current version of application expected
        ret |= ERROR_NVS_SETTINGS_MIN_VER_MIN_LESS;
    }
    if ((entry->header.version & CONFIG_NVS_SETTINGS_VER_MINOR_MASK) > (CONFIG_NVS_SETTINGS_VERSION & CONFIG_NVS_SETTINGS_VER_MINOR_MASK))
    {
        // the minor version of the stored entry is greater than the current version of application expected
        ret |= ERROR_NVS_SETTINGS_MIN_VER_MIN_GRTR;
    }
    if (entry->header.size < sizeof(nvs_settings_entry_t))
    {
        // the size of the stored entry is less than the current version of application expected
        ret |= ERROR_NVS_SETTINGS_ENTRY_SIZE_LESS;
    }
    if (entry->header.size > sizeof(nvs_settings_entry_t))
    {
        // the size of the stored entry is greater than the current version of application expected
        ret |= ERROR_NVS_SETTINGS_ENTRY_SIZE_GRTR;
    }
    if (nvs_settings_calc_crc32(0, (uint8_t*)&entry->settings, entry->header.size - sizeof(entry->header)) != entry->header.crc32)
    {
        // invalid crc
        ret |= ERROR_NVS_SETTINGS_ENTRY_CRC_INVAL;
    }

    DBG_PRINTF("ret = 0x%02X", ret);

    return ret;
}

// returns 0xFFFFFFFF if not found
static uint32_t nvs_settings_find_current_header_addr()
{
    __attribute__((aligned(4))) nvs_settings_header_t header;

    uint32_t first_non_empty_addr = 0xFFFFFFFF;
    for (uint32_t addr = CONFIG_NVS_SETTINGS_ADDR; addr < CONFIG_NVS_SETTINGS_ADDR + CONFIG_NVS_SETTINGS_SIZE - sizeof(header); addr += sizeof(header))
    {
        EEPROM_READ(addr, &header, sizeof(header));
        if (header.magic == CONFIG_NVS_SETTINGS_MAGIC)
        {
            DBG_PRINTF("Found header {m: 0x%X, v: 0x%02X, s: %d, c: 0x%08X} at addr 0x%X", header.magic, header.version, header.size, header.crc32, addr);
            first_non_empty_addr = addr;
            break;
        }
    }

    return first_non_empty_addr;
}

static uint32_t nvs_settings_init_general_settings(void)
{
    __attribute__((aligned(4))) nvs_settings_entry_t entry = {0xFF};

    uint32_t header_addr = nvs_settings_find_current_header_addr();

    if (header_addr == 0xFFFFFFFF)
    {
        // flash is probably clean now - init it firstly
        DBG_PRINTF("No header found, init defaults");
        header_addr = nvs_settings_init_defaults();
    }

    nvs_settings_read_cfg(&entry, header_addr);
    uint8_t err_flags = nvs_settings_validate_entry(&entry);

    // (!)TODO: add proper handlers
    if (err_flags)
    {
        // here we are able to handle errors
        if (err_flags & ERROR_NVS_SETTINGS_MAJ_VER_MISMATCH)
        {

        }
        if (err_flags & ERROR_NVS_SETTINGS_MIN_VER_MIN_LESS)
        {

        }
        if (err_flags & ERROR_NVS_SETTINGS_MIN_VER_MIN_GRTR)
        {

        }
        if (err_flags & ERROR_NVS_SETTINGS_ENTRY_SIZE_LESS)
        {

        }
        if (err_flags & ERROR_NVS_SETTINGS_ENTRY_SIZE_GRTR)
        {

        }
        if (err_flags & ERROR_NVS_SETTINGS_ENTRY_CRC_INVAL)
        {

        }
        DBG_PRINTF("Error: 0x%02X", err_flags);
        header_addr = nvs_settings_init_defaults(); // now we reset config to defaults on any error
    }

    return header_addr;
}

static uint32_t nvs_settings_get_current_header_addr()
{
    if (nvs_settings_current_header_addr == 0xFFFFFFFF)
    {
        nvs_settings_current_header_addr = nvs_settings_init_general_settings();
    }

    return nvs_settings_current_header_addr;
}

// (!!!) ATTENTION: for internal usage only!!! always use macro NVS_SETTING_GET instead of call!!!
// value - pointer to store value that was got from NVS
// size - size of setting
// offset - offset in nvs_settings_entry_t structure
void nvs_settings_setting_get(uint8_t* value, uint16_t size, uint16_t offset)
{
    __attribute__((aligned(4))) uint8_t aligned_buf[CONFIG_NVS_SETTINGS_MAX_SETTING_SIZE_BYTES];

    uint32_t start_addr = nvs_settings_get_current_header_addr();
    EEPROM_READ(start_addr + offset, aligned_buf, size);
    memcpy(value, aligned_buf, size);
}

// (!!!) ATTENTION: for internal usage only!!! always use macro NVS_SETTING_SET instead of call!!!
// value - pointer to value
// size - size of setting
// offset - offset in nvs_settings_entry_t structure
// apply_func - function to call for applying changes (should accept one argument = pointer to value)
void nvs_settings_setting_set(uint8_t* value, uint16_t size, uint16_t offset, void apply_func(uint8_t* value))
{
    __attribute__((aligned(4))) uint8_t aligned_buf[CONFIG_NVS_SETTINGS_MAX_SETTING_SIZE_BYTES];
    nvs_settings_setting_get(aligned_buf, size, offset); // get current value
    if (memcmp(aligned_buf, value, size))
    {
        // save the new value only if it differs from the current one
        memcpy(aligned_buf, value, size);
        __attribute__((aligned(4))) nvs_settings_entry_t entry = {0xFF};
        uint32_t start_addr = nvs_settings_get_current_header_addr();
        nvs_settings_read_cfg(&entry, start_addr);
        memcpy(((uint8_t*)&entry) + offset, value, size); // replace value of desired setting
        nvs_settings_write_cfg(&entry);
    }

    // call to apply_func if it is required by caller
    if (apply_func)
    {
        apply_func(value);
    }
}

/*********************************************************************
 * Apply functions for general settings
 * never call them directly, use only with MACROSes
 */
void nvs_settings_apply_ip_mode(uint8_t* ip_mode)
{
    if (!ip_mode)
    {
        return;
    }

    DBG_PRINTF("Set ip mode: %d", *ip_mode);
}
