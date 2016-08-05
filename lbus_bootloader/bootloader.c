/* LBUS based bootloader
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

/* LED blink intervals for bootloader mode (1 second / 0.33 seconds) */
#define BOOTLOADER_BLINK_INTERVAL ((CPU_SPEED/2)/65536)
#define BOOTLOADER_BLINK_INTERVAL_FW_BROKEN (((CPU_SPEED/2)/65536)/3)

/* Timeout for bootloader: 3 seconds */
#define BOOTLOADER_TIMEOUT (((CPU_SPEED/2)/65536)*3)

/* pointer to the firmware's config_section_s struct (if present) in flash
 *
 * The firmware is supposed to store its own version information, size and name
 * at a fixed defined position in flash.
 */
const struct config_section_s *firmware_config = (struct config_section_s *)(FW_CONFIG_ADDRESS);

/* variable blinking interval for LED when in bootloader mode */
uint16_t blink_interval = BOOTLOADER_BLINK_INTERVAL;

/* our own address, as managed by lbus.c
 *
 * We need to access it since in bootloader mode, a new address can be
 * programmed.
 */
extern uint8_t lbus_address;

/* Test firmware consistency against its own stored CRC32
 *
 * @return 0 if tested OK, -1 for an impossible stored size, -2 for a
 *         failed CRC32
 */
static int check_firmware(void) {
	uint32_t *fw;

	if(firmware_config->size > FW_MAX_SIZE)
		return -1;

	CRC_CR = 1; /* reset */
	for(fw = (uint32_t*)FW_ADDRESS; fw < &firmware_config->crc; fw++) {
		CRC_DR = *fw;
	}
	/* for the actual CRC stored in the firmware, skip that word and
	 * assume it to be 0. This makes calculating the checksum easier
	 * when flashing firmware
	 */
	CRC_DR = 0;
	fw++;
	for(; fw < (uint32_t*)(FW_ADDRESS + firmware_config->size); fw++) {
		CRC_DR = *fw;
	}
	if(CRC_DR != firmware_config->crc)
		return -2;

	return 0;
}

/* LBUS handler for READ_MEMORY request */
static void handle_READ_MEMORY(const uint8_t rbyte, const struct lbus_hdr *header, const unsigned int p) {
	LBUS_HANDLE_COMPLETE(static struct lbus_READ_MEMORY, d, p, rbyte) {
		uint32_t *src = (uint32_t*) d.address;
		lbus_start_tx();
		CRC_CR = 1; /* reset */
		for(uint16_t c = header->length - p - sizeof(uint32_t); c > 0; c-=4) {
			CRC_DR = *src;
			lbus_send32(src++);
		}
		uint32_t crc = CRC_DR;
		lbus_send32(&crc);
		lbus_end_pkg();
	}
}

/* LBUS handler for GET_DATA request */
static void handle_GET_DATA(const uint8_t rbyte, const struct lbus_hdr *header, const unsigned int p) {
	LBUS_HANDLE_COMPLETE(static struct lbus_GET_DATA, d, p, rbyte) {
		lbus_start_tx();
		switch(d.type) {
			case LBUS_DATA_STATUS:
				lbus_send(LBUS_STATE_IN_BOOTLOADER);
				break;
			case LBUS_DATA_ADDRESS:
				lbus_send(lbus_address);
				break;
			case LBUS_DATA_FIRMWARE_VERSION:
				lbus_send32(&firmware_config->version);
				break;
			case LBUS_DATA_BOOTLOADER_VERSION:
				{
					uint32_t bootloader_version = VERSION;
					lbus_send32(&bootloader_version);
				}
				break;
			case LBUS_DATA_FIRMWARE_NAME_LENGTH:
				{
					uint8_t l=0;
					for(const char* n=firmware_config->name; *n != '\0'; n++) 
						l++;
					lbus_send(l);
				}
				break;
			case LBUS_DATA_FIRMWARE_NAME:
				for(unsigned int i=0; i < header->length - p; i++)
					lbus_send(firmware_config->name[i]);
				break;
			default:
				for(int i=p; i<header->length; i++) 
					lbus_send(0);
		}
		lbus_end_pkg();
	}
}

/* LBUS handler for SET_ADDRESS request
 *
 * Will transmit back 1 status byte:
 *  0: success
 *  other: error storing address in config section in flash memory
 */
static void handle_SET_ADDRESS(const uint8_t rbyte, const struct lbus_hdr *header, const unsigned int p) {
	(void)header;
	LBUS_HANDLE_COMPLETE(static struct lbus_SET_ADDRESS, d, p, rbyte) {
		lbus_start_tx();
		int8_t result = config_set_uint32(CONFIG_LBUS_ADDRESS, d.naddr);
		lbus_send(result);
		if(!result)
			lbus_address = d.naddr; /* update address in LBUS handling */
		lbus_end_pkg();
	}
}

/* LBUS handler for FLASH_FIRMWARE request
 *
 * Will transmit back 1 status byte:
 *  0: success
 *  1: bad page_id
 *  2: bad CRC
 */
static void handle_FLASH_FIRMWARE(const uint8_t rbyte, const struct lbus_hdr *header, const unsigned int p) {
	(void)header;
	LBUS_HANDLE_COMPLETE(static struct lbus_FLASH_FIRMWARE, d, p, rbyte) {
		lbus_start_tx();
		uint32_t dst = FLASH_START + d.page_id*FLASH_PAGE_SIZE;
		/* check if requested target page is valid */
		if(dst < FW_ADDRESS || dst >= CONFIG_ADDRESS) {
			lbus_send(1);
			goto done;
		}
		/* check CRC32 of received page data */
		CRC_CR = 1; /* reset */
		for(int i=0; i<100; i++) __asm("nop");
		for(int i=0; i<FLASH_PAGE_SIZE; i+=4) CRC_DR = *((uint32_t*)(d.data+i));
		if(CRC_DR != d.crc) {
			lbus_send(2);
			goto done;
		}
		/* flash page */
		flash_unlock();
		flash_erase_page(dst);
		flash_wait_for_last_operation();
		if(*(uint32_t*)dst != 0xFFFFFFFF) {
			lbus_send(3);
			goto done;
		}
		for(int i=0; i<FLASH_PAGE_SIZE; i+=4) {
			const uint32_t w = *((uint32_t*)(d.data+i));
			flash_program_word(dst+i, w);
			flash_wait_for_last_operation();
			if(*(uint32_t*)(dst+i) != w) {
				lbus_send(4);
				goto done;
			}
		}
		flash_wait_for_last_operation();
		flash_lock();
		lbus_send(0);
done:
		lbus_end_pkg();
	}
}

/* Trigger a reboot and request a normal boot
 *
 * This will avoid staying in bootloader mode when BOOT1 pin is set
 */
static void reset_to_firmware(void) {
	/* bootloader operation mode */
	BKP_DR1 = BOOTFLAG_ENFORCE_NORMAL_BOOT;
	/* software reset */
	SCB_AIRCR = SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
	while (1);
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
		case ERASE_CONFIG:
			config_erase();
			break;
		case SET_ADDRESS:
			return handle_SET_ADDRESS;
		case READ_MEMORY:
			return handle_READ_MEMORY;
		case FLASH_FIRMWARE:
			return handle_FLASH_FIRMWARE;
		case RESET_TO_FIRMWARE:
			reset_to_firmware();
			break;
		case RESET_TO_BOOTLOADER:
			/* disable timeout */
			TIM_CR1(TIM3) &= ~TIM_CR1_CEN;
			break;
	}
	return NULL;
}

/* boot firmware */
static void run_firmware(void) {
	/* shut down all devices we might have used in bootloader mode */
	RCC_AHBENR &= ~(RCC_AHBENR_CRCEN );
	RCC_APB2ENR &= ~(RCC_APB2ENR_USART1EN | RCC_APB2ENR_SPI1EN | RCC_APB2ENR_TIM1EN | RCC_APB2ENR_ADC2EN | RCC_APB2ENR_ADC1EN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN);
	RCC_APB1ENR &= ~(RCC_APB1ENR_USART3EN | RCC_APB1ENR_USART2EN | RCC_APB1ENR_SPI3EN | RCC_APB1ENR_SPI2EN | RCC_APB1ENR_WWDGEN | RCC_APB1ENR_TIM4EN | RCC_APB1ENR_TIM3EN | RCC_APB1ENR_TIM2EN);
	RCC_AHBENR &= ~(RCC_AHBENR_CRCEN);
	/* send reset to all devices */
	RCC_APB2RSTR |= (RCC_APB2RSTR_USART1RST | RCC_APB2RSTR_SPI1RST | RCC_APB2RSTR_TIM1RST | RCC_APB2RSTR_ADC2RST | RCC_APB2RSTR_ADC1RST | RCC_APB2RSTR_IOPCRST | RCC_APB2RSTR_IOPBRST | RCC_APB2RSTR_IOPARST | RCC_APB2RSTR_AFIORST);
	RCC_APB1RSTR |= (RCC_APB1RSTR_USART3RST | RCC_APB1RSTR_USART2RST | RCC_APB1RSTR_SPI3RST | RCC_APB1RSTR_SPI2RST | RCC_APB1RSTR_WWDGRST | RCC_APB1RSTR_TIM4RST | RCC_APB1RSTR_TIM3RST | RCC_APB1RSTR_TIM2RST);
	RCC_APB2RSTR = 0;
	RCC_APB1RSTR = 0;
	/* disable all interrupts and priorities */
	NVIC_ICER(0) = 0xFFFFFFFF;
	NVIC_ICER(1) = 0xFFFFFFFF;
	NVIC_ICER(2) = 0xFFFFFFFF;
	NVIC_ICPR(0) = 0xFFFFFFFF;
	NVIC_ICPR(1) = 0xFFFFFFFF;
	NVIC_ICPR(2) = 0xFFFFFFFF;

	/* store bootloader version in BKP_DR2,3 */
	BKP_DR2 = VERSION & 0xFFFF;
	BKP_DR3 = VERSION >> 16;

	/* Set vector table base address. */
	SCB_VTOR = FW_ADDRESS & 0xFFFF;
	/* Initialise master stack pointer. */
	asm volatile("msr msp, %0"::"g"
			 (*(volatile uint32_t *)FW_ADDRESS));
	/* Jump to application. */
	(*(void (**)())(FW_ADDRESS + 4))();
}

/* blink timer ISR */
void tim4_isr(void) {
	gpio_toggle(GPIOC, GPIO13);
	TIM_SR(TIM4) &= ~TIM_SR_UIF;
}

/* set up LED blinking timer and pin */
static void setup_blink(void) {
	RCC_APB1RSTR |= RCC_APB1RSTR_TIM4RST;
	RCC_APB1RSTR &= ~RCC_APB1RSTR_TIM4RST;
	TIM_PSC(TIM4) = 0xFFFF;
	TIM_ARR(TIM4) = blink_interval;
	TIM_DIER(TIM4) |= TIM_DIER_UIE;

	/* onboard LED */
	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);

	nvic_enable_irq(NVIC_TIM4_IRQ);

	TIM_CR1(TIM4) |= TIM_CR1_CEN;
}

/* bootloader main function
 *
 * when timeout is not 0, it specifies a timeout after which bootloader
 * mode is stopped and firmware is booted. This timeout can be interrupted
 * when an LBUS RESET_TO_BOOTLOADER packet is received.
 */
static void run_bootloader(const uint16_t timeout) {
	lbus_init();

	if(timeout != 0) {
		RCC_APB1RSTR |= RCC_APB1RSTR_TIM3RST;
		RCC_APB1RSTR &= ~RCC_APB1RSTR_TIM3RST;
		TIM_PSC(TIM3) = 0xFFFF;
		TIM_CR1(TIM3) |= TIM_CR1_CEN;
	}

	setup_blink();

	while (1) {
		__asm("wfe");
		if(TIM3_CNT > timeout)
			run_firmware();
	}
}

int main(void) {
	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	/* enable devices */
	RCC_APB2ENR |= (RCC_APB2ENR_USART1EN | RCC_APB2ENR_TIM1EN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN);
	RCC_APB1ENR |= (RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN | RCC_APB1ENR_USART3EN | RCC_APB1ENR_USART2EN | RCC_APB1ENR_TIM4EN | RCC_APB1ENR_TIM3EN | RCC_APB1ENR_TIM2EN);
	RCC_AHBENR |= (RCC_AHBENR_CRCEN);

	/* disable backup domain write protection */
	PWR_CR |= PWR_CR_DBP;

	/* when the firmware stored in flash is invalid, switch into
	 * bootloader mode unconditionally
	 */
	if(check_firmware()) {
		blink_interval = BOOTLOADER_BLINK_INTERVAL_FW_BROKEN;
		run_bootloader(0);
	}

	/* when bootloader mode is requested by firmware by setting BKP_DR1,
	 * just switch into bootloader mode without timeout.
	 */
	if(BKP_DR1 == BOOTFLAG_GOTO_BOOTLOADER)
		run_bootloader(0);

	/* when bootloader mode is requested by setting BOOT1, switch
	 * into bootloader mode if it is not explicitly prohibited by
	 * software by setting BKP_DR1 accordingly
	 */
	if(gpio_get(GPIOB, GPIO2) && BKP_DR1 != BOOTFLAG_ENFORCE_NORMAL_BOOT)
		run_bootloader(0);

	/* standard behaviour: switch into bootloader mode for a short
	 * amount of time after each (hardware or software) reset.
	 *
	 * Allows to deal with unresponsive, but valid firmware.
	 */
	run_bootloader(BOOTLOADER_TIMEOUT);
}
