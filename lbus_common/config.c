/* Config store in flash
 *
 * Copyright (c) 2016 Hans-Werner Hilse <hwhilse@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <stddef.h>
#include <libopencm3/stm32/flash.h>
#include "platform.h"
#include "config.h"

/* find configuration item in config space in flash
 * will return the last found matching item (except for CONFIG_UNSET type,
 * in which case it will return the "first" such item - i.e. the first
 * slot not yet filled with config data).
 * If no config for the given type is found, NULL will be returned.
 */
struct config_item* config_find_item(const uint32_t type) {
	struct config_item* found = NULL;
	void *config = (void*)CONFIG_ADDRESS;
	while(config < (void*)(CONFIG_ADDRESS + CONFIG_SIZE)) {
		struct config_item *item = (struct config_item *)config;
		if(item->type == type)
			found = item;
		if(item->type == CONFIG_UNSET) // erased flash
			break;
		/* skip to next 4-byte aligned item */
		config += (item->length + sizeof(uint32_t)*2 + 3) & (~(3));
	}
	return found;
}

/* convenience function for accessing configuration consisting of one word of data */
uint32_t config_get_uint32(const uint32_t type) {
	void *dst = config_find_item(type);
	if(dst == NULL) return 0;
	return *((uint32_t*)(dst + sizeof(struct config_item)));
}

/* write a new configuration item to config space in flash memory.
 * data will be 4-byte aligned when writing, but original length information
 * will be retained.
 *
 * @return 0 if successful, -2 when no space for storing data is available,
 *         -1 when there is not enough space for the requested length
 */
int config_write(const uint32_t type, const uint32_t length, const void* data) {
	uint32_t *dst = (uint32_t*) config_find_item(CONFIG_UNSET);
	if(dst == NULL)
		return -2;
	if(dst + length + 3 >= (uint32_t*)(CONFIG_ADDRESS + CONFIG_SIZE))
		return -1;

	flash_unlock();
	/* write header information */
	flash_program_word((uint32_t)(dst++), type);
	flash_program_word((uint32_t)(dst++), length);
	/* word-wise reading/writing */
	uint32_t p = 0;
	for(; p + 3 < length; p+=sizeof(uint32_t)) {
		uint32_t v = *((uint32_t*)(data+p));
		flash_program_word((uint32_t)(dst++), v);
	}
	/* read remainder of partial last word */
	if(p < length) {
		uint32_t v = ((uint8_t*)data)[p++];
		if(p < length)
			v = v | (((uint8_t*)data)[p++] << 8);
		if(p < length)
			v = v | (((uint8_t*)data)[p++] << 16);
		flash_program_word((uint32_t)(dst++), v);
	}
	flash_lock();
	return 0;
}

/* convenience function to store a single word of configuration */
int config_set_uint32(const uint32_t type, const uint32_t value) {
	return config_write(type, sizeof(uint32_t), &value);
}

/* erase complete configuration store */
void config_erase(void) {
	flash_unlock();
	for(void *config = (void*)CONFIG_ADDRESS; config < (void*)(CONFIG_ADDRESS + CONFIG_SIZE); config += FLASH_PAGE_SIZE) {
		flash_erase_page((uint32_t)config);
	}
	flash_lock();
}
