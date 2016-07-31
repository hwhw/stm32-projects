/* Example firmware application for use with LBUS bootloader
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
#include <stdint.h>
#include <stddef.h>

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/stm32/f1/bkp.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/crc.h>
#include <libopencm3/stm32/flash.h>

#include "platform.h"
#include "lbus.h"
#include "config.h"

__attribute__ ((section(".config")))
struct config_section_s firmware_config = {
	.name = "boot-test",
	.version = VERSION,
};

#define CPU_SPEED 72000000

/* LBUS handler for GET_DATA request */
static void handle_GET_DATA(const uint8_t rbyte, const struct lbus_hdr *header, const unsigned int p) {
	LBUS_HANDLE_COMPLETE(static struct lbus_GET_DATA, d, p, rbyte) {
		lbus_start_tx();
		switch(d.type) {
			case LBUS_DATA_STATUS:
				lbus_send(LBUS_STATE_IN_FIRMWARE);
				break;
			case LBUS_DATA_ADDRESS: {
					uint32_t lbus_address = config_get_uint32(CONFIG_LBUS_ADDRESS);
					lbus_send(lbus_address);
				}
				break;
			case LBUS_DATA_FIRMWARE_VERSION:
				lbus_send32(&firmware_config.version);
				break;
			case LBUS_DATA_BOOTLOADER_VERSION:
				{
					uint32_t bootloader_version = (BKP_DR3 << 16) | BKP_DR2;
					lbus_send32(&bootloader_version);
				}
				break;
			case LBUS_DATA_FIRMWARE_NAME_LENGTH:
				{
					uint8_t l=0;
					for(const char* n=firmware_config.name; *n != '\0'; n++) 
						l++;
					lbus_send(l);
				}
				break;
			case LBUS_DATA_FIRMWARE_NAME:
				for(unsigned int i=0; i < header->length - p; i++)
					lbus_send(firmware_config.name[i]);
				break;
			default:
				for(int i=p; i<header->length; i++) 
					lbus_send(0);
		}
		lbus_end_pkg();
	}
}

/* LBUS request handling
 *
 * For some operations, further data is pending. In these cases,
 * a handler function for this data is returned. If no further data
 * is to be handled, NULL is returned.
 */
lbus_recv_func lbus_handler(const struct lbus_hdr *header) {
	switch(header->cmd) {
		case PING:
			lbus_start_tx();
			lbus_send(1);
			break;
		case GET_DATA:
			return handle_GET_DATA;
		case RESET_TO_BOOTLOADER:
			lbus_reset_to_bootloader();
			break;
	}
	return NULL;
}

static void gpio_setup(void) {
	rcc_periph_clock_enable(RCC_GPIOC);
	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);
}

void tim3_isr(void) {
	gpio_toggle(GPIOC, GPIO13);
	TIM_SR(TIM3) &= ~TIM_SR_UIF;
}

int main(void) {
	rcc_clock_setup_in_hse_8mhz_out_72mhz();
	rcc_periph_clock_enable(RCC_TIM3);

	gpio_setup();

	lbus_init();

	timer_reset(TIM3);
	nvic_enable_irq(NVIC_TIM3_IRQ);
	timer_set_mode(TIM3, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_set_prescaler(TIM3, ((CPU_SPEED/2)/10000)); // 1 tick = 100 usec
	timer_set_period(TIM3, 50000);
	timer_continuous_mode(TIM3);
	TIM_DIER(TIM3) |= TIM_DIER_UIE;
	timer_enable_counter(TIM3);

	while(1) __asm("wfe");
	return 0;
}
