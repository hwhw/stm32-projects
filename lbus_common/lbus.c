/* LBUS main code
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
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/stm32/f1/bkp.h>
#include <libopencm3/cm3/scb.h>

#include "lbus.h"
#include "platform.h"
#include "config.h"

static struct lbus_hdr lbus_header;
static volatile int pkg_pos;
static lbus_recv_func recv_func;
uint8_t lbus_address;

/* Standard receive callback
 *
 * This is used for receiving the packet header. When the packet header
 * is fully received, it will call the lbus_handler() function to handle
 * the packet - either directly or by setting a new receive callback
 * for further packet data
 */
static void recv(const uint8_t rbyte, const struct lbus_hdr* hdr, const unsigned int p) {
	if(p <= sizeof(*hdr)) {
		((uint8_t*)hdr)[p-1] = rbyte;
	}
	if(p == sizeof(*hdr)) {
		if(hdr->addr == 0xFF || hdr->addr == lbus_address) {
			recv_func = lbus_handler(hdr);
		} else {
			recv_func = NULL;
		}
	}
	/* pkg_pos might be more actual here than the <p> value, when the lbus_handler
	 * has sent some data.
	 */
	if(pkg_pos >= 2 && pkg_pos == hdr->length) {
		/* in any case reset state when a packet is complete (as indicated by the length) */
		lbus_end_pkg();
	}
}

static bool transmitting = false;

/* End transmit mode, switch back to receive mode
 *
 * This sets state back to receive mode. Packet timeout timer is deactivated,
 * receive buffer position is reset, standard receive callback is put into
 * order.
 */
void lbus_end_pkg(void) {
	if(transmitting) {
		/* wait until everything has been sent */
		while((USART_SR(LBUS_USART) & USART_SR_TC) == 0) __asm("nop");
		/* tie low MAX485 DE/~RE */
		gpio_clear(LBUS_DE_GPIO, LBUS_DE_PIN);
		gpio_clear(LBUS_RE_GPIO, LBUS_RE_PIN);
		/* enable RX interrupt: */
		USART_CR1(LBUS_USART) |= USART_CR1_RXNEIE;
		transmitting = false;
	}
	TIM_CR1(TIM1) &= ~TIM_CR1_CEN;
	pkg_pos = 0;
	recv_func = recv;
}

/* USART interrupt service routine
 *
 * This ISR handles received bytes on the USART
 */
void LBUS_USART_ISR(void) {
	/* Check if we were called because of RXNE. */
	if((USART_SR(LBUS_USART) & USART_SR_RXNE) != 0) {
		/* reset timeout timer */
		TIM1_CNT = 0;
		/* start timer when a new packet starts (avoids lots of timer interrupts on an idle lbus)*/
		if(pkg_pos == 0) {
			TIM_CR1(TIM1) |= TIM_CR1_CEN;
		}
		/* use recv_func to handle incoming bytes */
		pkg_pos++;
		/* call receive handler callback function, if set */
		if(recv_func) {
			recv_func(usart_recv(LBUS_USART), &lbus_header, pkg_pos);
		} else {
			/* otherwise, just consume the data */
			usart_recv(LBUS_USART);
		}
	}
}

/* Timeout timer ISR
 *
 * will enforce end of packet receive when the timeout has been hit
 */
void tim1_up_isr(void) {
	lbus_end_pkg();
	TIM_SR(TIM1) &= ~TIM_SR_UIF;
}

/* Switch operation from receive to transmit
 *
 * disable timeout timer (as the new transmitting entity, we're in charge
 * now of keeping the timing), receive interrupts, switch MAX485 DE/~RE mode.
 * Will also enforce a short wait in order to allow the other party to switch
 * their mode, too.
 */
void lbus_start_tx(void) {
	/* disable RX interrupt: */
	USART_CR1(LBUS_USART) &= ~USART_CR1_RXNEIE;
	/* stop timeout timer */
	TIM_CR1(TIM1) &= ~TIM_CR1_CEN;
	/* toggle MAX485 driver/receiver enable */
	gpio_set(LBUS_RE_GPIO, LBUS_RE_PIN);
	gpio_set(LBUS_DE_GPIO, LBUS_DE_PIN);
	/* now wait a bit */
	/*
	TIM1_CNT = 0;
	TIM_CR1(TIM1) |= TIM_CR1_CEN;
	while(TIM1_CNT < LBUS_TX_WAIT) { __asm("nop"); }
	TIM_CR1(TIM1) &= ~TIM_CR1_CEN;
	*/
	///*
	for(int i=0; i<100; i++) __asm("nop");
	//*/
	transmitting = true;
}

/* Set up LBUS handling (RX/TX/DE/RE lines, ISRs etc.)
 *
 * Note that the GPIO device must be enabled already (RCC settings)
 */
void lbus_init(void) {
	lbus_address = config_get_uint32(CONFIG_LBUS_ADDRESS);

	RCC_APB2ENR |= (RCC_APB2ENR_TIM1EN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_AFIOEN);
	RCC_APB1ENR |= (RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN | RCC_APB1ENR_USART3EN);

	/* Enable the USART interrupt. */
	nvic_enable_irq(LBUS_USART_IRQ);

	/* Pin setup */
	gpio_set_mode(LBUS_USART_GPIO, GPIO_MODE_OUTPUT_50_MHZ,
		GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, LBUS_USART_GPIO_TX);
	gpio_set_mode(LBUS_USART_GPIO, GPIO_MODE_INPUT,
		GPIO_CNF_INPUT_FLOAT, LBUS_USART_GPIO_RX);
	gpio_set_mode(LBUS_DE_GPIO, GPIO_MODE_OUTPUT_50_MHZ,
		GPIO_CNF_OUTPUT_PUSHPULL, LBUS_DE_PIN);
	gpio_set_mode(LBUS_RE_GPIO, GPIO_MODE_OUTPUT_50_MHZ,
		GPIO_CNF_OUTPUT_PUSHPULL, LBUS_RE_PIN);

	/* Setup UART parameters. */
	usart_set_baudrate(LBUS_USART, LBUS_BAUDRATE);

	/* empty USART receive buffer */
	while((USART_SR(LBUS_USART) & USART_SR_RXNE) != 0)
		usart_recv(LBUS_USART);

	/* configure and enable USART. */
	USART_CR1(LBUS_USART) |= USART_CR1_RXNEIE | USART_CR1_RE | USART_CR1_TE | USART_CR1_UE;

	/* set up a timer for timing out packet receives: */
	RCC_APB2RSTR |= RCC_APB2RSTR_TIM1RST;
	RCC_APB2RSTR &= ~RCC_APB2RSTR_TIM1RST;
	nvic_enable_irq(NVIC_TIM1_UP_IRQ);
	TIM_PSC(TIM1) = ((CPU_SPEED/2)/10000); // 1 tick = 100 usec
	TIM_ARR(TIM1) = LBUS_TIMEOUT;
	TIM_EGR(TIM1) |= TIM_EGR_UG;
	TIM_DIER(TIM1) |= TIM_DIER_UIE;

	/* mis-use end-of-packet function to enforce state reset */
	lbus_end_pkg();
}

/* Trigger a reboot and request a normal boot */
void lbus_reset_to_bootloader(void) {
	/* disable backup domain write protection */
	PWR_CR |= PWR_CR_DBP;
	/* bootloader operation mode */
	BKP_DR1 = BOOTFLAG_GOTO_BOOTLOADER;
	/* software reset */
	SCB_AIRCR = SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
	while (1);
}

/* Send a single byte over LBUS */
void lbus_send(const uint8_t txbyte) {
	usart_send_blocking(LBUS_USART, txbyte);
	pkg_pos++;
}

/* Send a series of bytes from a buffer over LBUS */
void lbus_send_buf(const void *buf, const int length) {
	for(int i=0; i<length; i++)
		usart_send_blocking(LBUS_USART, ((uint8_t*)buf)[i]);
	pkg_pos += length;
}
