/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2009 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2013 Stephen Dwyer <scdwyer@ualberta.ca>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/timer.h>
#include <stdio.h>
#include <errno.h>

int _write(int file, char *ptr, int len);

static void clock_setup(void)
{
	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	/* Enable clocks for GPIO port A (for GPIO_USART2_TX) and USART2. */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_AFIO);

	/* Enable TIM3 Periph */
	rcc_periph_clock_enable(RCC_TIM3);
}

#define LED_COUNT 5

#define TICK_NS (1000/72)
#define WS0 (350 / TICK_NS)
#define WS1 (800 / TICK_NS)
#define WSP (1300 / TICK_NS)
#define WSL (20000 / TICK_NS)
const uint8_t values[] = {
	WS0, WS1, WS1, WS1, WS1, WS1, WS1, WS1, // 0b11111111
	WS0, WS0, WS0, WS0, WS0, WS0, WS0, WS0, // 0b00000000
	WS0, WS0, WS0, WS0, WS0, WS0, WS0, WS0, // 0b00000000

	WS0, WS0, WS0, WS0, WS0, WS0, WS0, WS0, // 0b00000000
	WS0, WS1, WS1, WS1, WS1, WS1, WS1, WS1, // 0b11111111
	WS0, WS0, WS0, WS0, WS0, WS0, WS0, WS0, // 0b00000000

	WS0, WS0, WS0, WS0, WS0, WS0, WS0, WS0, // 0b00000000
	WS0, WS0, WS0, WS0, WS0, WS0, WS0, WS0, // 0b00000000
	WS0, WS1, WS1, WS1, WS1, WS1, WS1, WS1, // 0b11111111

	WS0, WS1, WS1, WS1, WS1, WS1, WS1, WS1, // 0b11111111
	WS0, WS0, WS0, WS0, WS0, WS0, WS0, WS0, // 0b00000000
	WS0, WS0, WS0, WS0, WS0, WS0, WS0, WS0, // 0b00000000

	WS0, WS0, WS0, WS0, WS0, WS0, WS0, WS0, // 0b00000000
	WS0, WS1, WS1, WS1, WS1, WS1, WS1, WS1, // 0b11111111
	WS0, WS0, WS0, WS0, WS0, WS0, WS0, WS0, // 0b00000000

	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

static void pwm_setup(void) {

	/* Configure GPIOs: SS=PA4, SCK=PA5, MISO=PA6 and MOSI=PA7 */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM3_CH2 );

	timer_reset(TIM3);

	timer_set_mode(TIM3, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);

	timer_disable_oc_output(TIM3, TIM_OC2);
	timer_set_oc_mode(TIM3, TIM_OC2, TIM_OCM_PWM1);
	timer_disable_oc_clear(TIM3, TIM_OC2);
	timer_set_oc_value(TIM3, TIM_OC2, 0);
	timer_enable_oc_preload(TIM3, TIM_OC2);
	timer_set_oc_polarity_high(TIM3, TIM_OC2);
	timer_enable_oc_output(TIM3, TIM_OC2);

	timer_enable_preload(TIM3);
	timer_continuous_mode(TIM3);
	timer_set_period(TIM3, WSP);

	timer_enable_counter(TIM3);
}

int main(void)
{
	int i;

	clock_setup();
	pwm_setup();

	/* Blink the LED (PA8) on the board with every transmitted byte. */
	while (1) {
		for(i=0; i<(LED_COUNT*3*8+40); i++) {
			TIM_CCR2(TIM3) = values[i];
			TIM3_SR &= ~TIM_SR_UIF;
			while(!(TIM3_SR & TIM_SR_UIF)) { /* wait... */ }
		}
	}

	return 0;
}
