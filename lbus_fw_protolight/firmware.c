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
	.name = "protolight-controller",
	.version = VERSION,
};

#define CPU_SPEED 72000000

static void clock_setup(void)
{
	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	/* Enable clocks for GPIO ports */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_AFIO);

	/* Enable timer peripherals */
	rcc_periph_clock_enable(RCC_TIM2);
	rcc_periph_clock_enable(RCC_TIM3);
	rcc_periph_clock_enable(RCC_TIM4);
}

// 16bit @ 100 Hz
#define PWM_FREQ 100

static void oc_setup(uint32_t timer, uint32_t oc) {
	timer_disable_oc_output(timer, oc);
	timer_set_oc_mode(timer, oc, TIM_OCM_PWM1);
	timer_disable_oc_clear(timer, oc);
	timer_set_oc_value(timer, oc, 0);
	timer_enable_oc_preload(timer, oc);
	timer_set_oc_polarity_low(timer, oc);
	timer_enable_oc_output(timer, oc);
}
static void timer_setup(uint32_t timer) {
	timer_reset(timer);

	timer_set_mode(timer, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_set_prescaler(timer, ((72000000/65536) / PWM_FREQ));
	timer_continuous_mode(timer);
	timer_set_period(timer, 65535);

	oc_setup(timer, TIM_OC1);
	oc_setup(timer, TIM_OC2);
	oc_setup(timer, TIM_OC3);
	oc_setup(timer, TIM_OC4);

	timer_enable_counter(timer);
}
static void pwm_setup(void) {
	// PA0
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM2_CH1_ETR );
	// PA1
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM2_CH2 );
	// PA2
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM2_CH3 );
	// PA3
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM2_CH4 );

	// PA6
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM3_CH1 );
	// PA7
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM3_CH2 );
	// PB0
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM3_CH3 );
	// PB1
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM3_CH4 );

	// PB6
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM4_CH1 );
	// PB7
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM4_CH2 );
	// PB8
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM4_CH3 );
	// PB9
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM4_CH4 );

	timer_setup(TIM2);
	// wait a bit before starting next timer
	// this way, the PWM pulses start at about 0, 33, 66% for R, G, B
	while(timer_get_counter(TIM2) < (65535/3)) { /* wait */ };
	timer_setup(TIM3);
	while(timer_get_counter(TIM2) < (2*65535/3)) { /* wait */ };
	timer_setup(TIM4);
}

static uint16_t values[4*3];

static void commit(void) {
	timer_set_oc_value(TIM2, TIM_OC1, values[0]);
	timer_set_oc_value(TIM3, TIM_OC1, values[1]);
	timer_set_oc_value(TIM4, TIM_OC1, values[2]);
	timer_set_oc_value(TIM2, TIM_OC2, values[3]);
	timer_set_oc_value(TIM3, TIM_OC2, values[4]);
	timer_set_oc_value(TIM4, TIM_OC2, values[5]);
	timer_set_oc_value(TIM2, TIM_OC3, values[6]);
	timer_set_oc_value(TIM3, TIM_OC3, values[7]);
	timer_set_oc_value(TIM4, TIM_OC3, values[8]);
	timer_set_oc_value(TIM2, TIM_OC4, values[9]);
	timer_set_oc_value(TIM3, TIM_OC4, values[10]);
	timer_set_oc_value(TIM4, TIM_OC4, values[11]);
}

static void handle_LED_SET_16BIT(const uint8_t rbyte, const struct lbus_hdr *header, const int p) {
	static uint16_t data[4*3+1]; 
	((uint8_t*)&data)[p-sizeof(struct lbus_hdr)-1] = rbyte;
	if(p == header->length) {
		uint16_t offset = data[0];
		uint16_t num_values = ((p-sizeof(struct lbus_hdr)-1) >> 1) - 1;
		int n = 1;
		while(num_values > 0) {
			if(offset < 12) {
				values[offset++] = data[n];
				num_values--;
				n++;
			}
		}
	}
}

/* LBUS handler for GET_DATA request */
static void handle_GET_DATA(const uint8_t rbyte, const struct lbus_hdr *header, const int p) {
	LBUS_HANDLE_COMPLETE(static struct lbus_GET_DATA, d, p, rbyte) {
		lbus_start_tx();
		switch(d.type) {
			case LBUS_DATA_STATUS:
				lbus_send(LBUS_STATE_IN_FIRMWARE);
				break;
			case LBUS_DATA_ADDRESS:
				lbus_send(0); //TODO
				break;
			case LBUS_DATA_FIRMWARE_VERSION:
				lbus_send32(&firmware_config.version);
				break;
			case LBUS_DATA_BOOTLOADER_VERSION:
				{
					uint32_t bootloader_version = 0; //TODO
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
				for(int i=0; i < header->length - p; i++)
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
			lbus_end_pkg();
			break;
		case GET_DATA:
			return handle_GET_DATA;
		case LED_COMMIT:
			commit();
			break;
		case LED_SET_16BIT:
			return handle_LED_SET_16BIT;
		case RESET_TO_BOOTLOADER:
			break;
	}
	return NULL;
}
int main(void)
{
	clock_setup();

	// LED
	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);
	pwm_setup();

	lbus_init();

	while (1) {
		__asm__("wfe");
	}
}
