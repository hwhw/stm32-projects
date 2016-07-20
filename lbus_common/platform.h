#ifndef _PLATFORM_H_
#define _PLATFORM_H_
#include <stdint.h>
#include <libopencm3/cm3/vector.h>

/* start address of the flash memory */
#define FLASH_START	0x08000000

/* size of reserved space for bootloader */
#define BOOTLOADER_SIZE	0x00001000

/* start address of main firmware */
#define FW_ADDRESS	(FLASH_START + BOOTLOADER_SIZE)

/* address of main firmware's config information section */
#define FW_CONFIG_ADDRESS (FW_ADDRESS + sizeof(vector_table_t))

/* address of configuration data space in flash */
#define CONFIG_ADDRESS	0x0800C000

/* size of configuration data space */
#define CONFIG_SIZE	0x00004000

/* maximum size for main firmware */
#define FW_MAX_SIZE	(CONFIG_ADDRESS - FW_ADDRESS)

/* size of a single page in flash */
#define FLASH_PAGE_SIZE 1024

/* CPU speed in MHz */
#define CPU_SPEED 72000000

/* flags for BKP_DR1 to specify boot mode */
#define BOOTFLAG_NORMAL_BOOT 0
#define BOOTFLAG_GOTO_BOOTLOADER 1
#define BOOTFLAG_ENFORCE_NORMAL_BOOT 2

/* firmware config information */
struct config_section_s {
	uint32_t version;
	uint32_t size;
	uint32_t crc;
	char name[];
};

#endif // _PLATFORM_H_
