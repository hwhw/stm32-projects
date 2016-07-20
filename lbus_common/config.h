#ifndef _CONFIG_H_
#define _CONFIG_H_
#include <stdint.h>
#include "platform.h"

enum config_type {
	CONFIG_LBUS_ADDRESS = 1,

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
struct config_item* find_item(const uint32_t type);
int config_write(const uint32_t type, const uint32_t length, const void* data);
void config_erase(void);

#endif // _CONFIG_H_
