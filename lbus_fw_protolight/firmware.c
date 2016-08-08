/* PWM controller for 12 channels
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
static const uint16_t* lut[4*3];

/* ((n+1)^2 - 1) by default: */
static const uint16_t default_lut[256] = {
	0, 3, 8, 15, 24, 35, 48, 63, 80, 99, 120, 143, 168, 195, 224, 255, 288,
	323, 360, 399, 440, 483, 528, 575, 624, 675, 728, 783, 840, 899, 960, 1023,
	1088, 1155, 1224, 1295, 1368, 1443, 1520, 1599, 1680, 1763, 1848, 1935,
	2024, 2115, 2208, 2303, 2400, 2499, 2600, 2703, 2808, 2915, 3024, 3135,
	3248, 3363, 3480, 3599, 3720, 3843, 3968, 4095, 4224, 4355, 4488, 4623,
	4760, 4899, 5040, 5183, 5328, 5475, 5624, 5775, 5928, 6083, 6240, 6399,
	6560, 6723, 6888, 7055, 7224, 7395, 7568, 7743, 7920, 8099, 8280, 8463,
	8648, 8835, 9024, 9215, 9408, 9603, 9800, 9999, 10200, 10403, 10608, 10815,
	11024, 11235, 11448, 11663, 11880, 12099, 12320, 12543, 12768, 12995,
	13224, 13455, 13688, 13923, 14160, 14399, 14640, 14883, 15128, 15375,
	15624, 15875, 16128, 16383, 16640, 16899, 17160, 17423, 17688, 17955,
	18224, 18495, 18768, 19043, 19320, 19599, 19880, 20163, 20448, 20735,
	21024, 21315, 21608, 21903, 22200, 22499, 22800, 23103, 23408, 23715,
	24024, 24335, 24648, 24963, 25280, 25599, 25920, 26243, 26568, 26895,
	27224, 27555, 27888, 28223, 28560, 28899, 29240, 29583, 29928, 30275,
	30624, 30975, 31328, 31683, 32040, 32399, 32760, 33123, 33488, 33855,
	34224, 34595, 34968, 35343, 35720, 36099, 36480, 36863, 37248, 37635,
	38024, 38415, 38808, 39203, 39600, 39999, 40400, 40803, 41208, 41615,
	42024, 42435, 42848, 43263, 43680, 44099, 44520, 44943, 45368, 45795,
	46224, 46655, 47088, 47523, 47960, 48399, 48840, 49283, 49728, 50175,
	50624, 51075, 51528, 51983, 52440, 52899, 53360, 53823, 54288, 54755,
	55224, 55695, 56168, 56643, 57120, 57599, 58080, 58563, 59048, 59535,
	60024, 60515, 61008, 61503, 62000, 62499, 63000, 63503, 64008, 64515,
	65024, 65535 };

static void read_lut(void) {
	for(int i=0; i<4*3; i++) {
		const struct config_item* led_lut = config_find_item(CONFIG_LED_LUT8TO16 + i);
		if(led_lut != NULL) {
			lut[i] = (uint16_t*)((void*)&led_lut + sizeof(struct config_item));
		} else {
			lut[i] = default_lut;
		}
	}
}

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

static void handle_LED_SET_16BIT(const uint8_t rbyte, const struct lbus_hdr *header, const unsigned int p) {
	static struct {
		uint16_t offset;
		uint16_t data[4*3];
	} in;
	if(sizeof(struct lbus_hdr)+sizeof(in) >= p)
		((uint8_t*)&in)[p-sizeof(struct lbus_hdr)-1] = rbyte;
	if(p == header->length) {
		uint16_t n = (p-sizeof(struct lbus_hdr)-sizeof(uint16_t)) / sizeof(uint16_t);
		while(n-- > 0) {
			const int led = in.offset+n;
			if(led < 12) {
				values[led] = in.data[n];
			}
		}
		lbus_end_pkg();
	}
}

static void handle_LED_SET_8BIT(const uint8_t rbyte, const struct lbus_hdr *header, const unsigned int p) {
	static struct {
		uint16_t offset;
		uint8_t v[4*3];
	} data;
	if(sizeof(struct lbus_hdr)+sizeof(data) >= p)
		((uint8_t*)&data)[p-sizeof(struct lbus_hdr)-1] = rbyte;
	if(p == header->length) {
		uint16_t num_values = p-sizeof(struct lbus_hdr)-sizeof(uint16_t)-1;
		int n = 0;
		while(num_values > 0) {
			if(data.offset < 12) {
				values[data.offset] = lut[data.offset][data.v[n]];
				data.offset++;
				num_values--;
				n++;
			}
		}
		lbus_end_pkg();
	}
}

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
		case LED_COMMIT:
			commit();
			break;
		case LED_SET_16BIT:
			return handle_LED_SET_16BIT;
		case LED_SET_8BIT:
			return handle_LED_SET_8BIT;
		case RESET_TO_BOOTLOADER:
			lbus_reset_to_bootloader();
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

	read_lut();

	lbus_init();

	while (1) {
		__asm__("wfe");
	}
}
