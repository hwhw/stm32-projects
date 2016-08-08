#ifndef _LBUS_DATA_H_
#define _LBUS_DATA_H_
#include <stdint.h>

#ifndef __packed
#define __packed __attribute__ ((packed))
#endif

#define PAGE_SIZE 1024

enum lbus_cmd {
	NOP = 0, // reserved
	PING,
	GET_DATA,
	LED_SET_16BIT = 10,
	LED_COMMIT,
	LED_SET_8BIT,
	RESET_TO_BOOTLOADER = 122,
	ERASE_CONFIG,
	SET_ADDRESS,
	READ_MEMORY,
	FLASH_FIRMWARE,
	RESET_TO_FIRMWARE
};

enum lbus_state {
	LBUS_STATE_IN_BOOTLOADER = 1,
	LBUS_STATE_IN_FIRMWARE = 2
};

enum lbus_data {
	LBUS_DATA_STATUS = 1,
	LBUS_DATA_ADDRESS,
	LBUS_DATA_FIRMWARE_VERSION,
	LBUS_DATA_BOOTLOADER_VERSION,
	LBUS_DATA_FIRMWARE_NAME_LENGTH,
	LBUS_DATA_FIRMWARE_NAME
};

struct lbus_hdr {
	// always send a packet length so controllers can skip
	// unknown commands
	uint16_t length;
	uint8_t addr;
	uint8_t cmd;
} __packed;

struct lbus_GET_DATA {
	uint16_t type;
} __packed;

struct lbus_LED_SET_16BIT {
	uint16_t led;
	uint16_t color[];
} __packed;

struct lbus_LED_SET_8BIT {
	uint16_t led;
	uint8_t color[];
} __packed;

struct lbus_SET_ADDRESS {
	uint8_t naddr;
} __packed;

struct lbus_READ_MEMORY {
	uint32_t address;
} __packed;

struct lbus_FLASH_FIRMWARE {
	uint16_t page_id;
	uint8_t data[PAGE_SIZE];
	uint32_t crc;
} __packed;

struct lbus_pkg {
	struct lbus_hdr hdr;
	union {
		struct lbus_GET_DATA GET_DATA;
		struct lbus_LED_SET_16BIT LED_SET_16BIT;
		struct lbus_LED_SET_8BIT LED_SET_8BIT;
		struct lbus_SET_ADDRESS SET_ADDRESS;
		struct lbus_READ_MEMORY READ_MEMORY;
		struct lbus_FLASH_FIRMWARE FLASH_FIRMWARE;
	};
} __packed;

#endif
