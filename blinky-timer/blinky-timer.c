/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2009 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/timer.h>

#define CPU_SPEED 72000000

static void gpio_setup(void) {
	rcc_periph_clock_enable(RCC_GPIOC);
	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);
}

void tim1_up_isr(void) {
	//timer_disable_counter(TIM1);
	gpio_toggle(GPIOC, GPIO13);
	TIM_SR(TIM1) &= ~TIM_SR_UIF;
}

int main(void) {
	rcc_clock_setup_in_hse_8mhz_out_72mhz();
	rcc_periph_clock_enable(RCC_TIM1);

	gpio_setup();

	timer_reset(TIM1);
	nvic_enable_irq(NVIC_TIM1_UP_IRQ);
	timer_set_mode(TIM1, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_set_prescaler(TIM1, ((CPU_SPEED/2)/10000)); // 1 tick = 100 usec
	timer_set_period(TIM1, 20000);
	timer_continuous_mode(TIM1);
	TIM_DIER(TIM1) |= TIM_DIER_UIE;
	timer_enable_counter(TIM1);

	while(1) __asm("wfe");
	return 0;
}
