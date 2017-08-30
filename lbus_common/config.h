#ifndef _CONFIG_H_
#define _CONFIG_H_
#include <stdint.h>
#include "platform.h"

enum config_type {
	CONFIG_LBUS_ADDRESS = 1,

	/* config for PWM LEDs: */
	CONFIG_LED_GROUP = 0x00010000,
	CONFIG_LED_LUT8TO16 = 0x00011000, /* reserved up to 0x000110FF */
  CONFIG_LED_POLARITY = 0x00011100,

	/* 0xFFFFFFFF will be the value present after the config space has
	 * been erased, so it is reserved here
	 */
	CONFIG_UNSET = 0xFFFFFFFF
};

struct config_item {
	uint32_t type;
	uint32_t length;
};

/* config API */
uint32_t config_get_uint32(const uint32_t type);
int config_set_uint32(const uint32_t type, const uint32_t value);

/* low-level API: */
struct config_item* config_find_item(const uint32_t type);
int config_write(const uint32_t type, const uint32_t length, const void* data);
void config_erase(void);

#endif // _CONFIG_H_
