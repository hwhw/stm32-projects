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
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

// minimum ID offset is 0x100 (first ID byte mustn't be 0x00)
#define ID_OFFSET 0x100

int _write(int file, char *ptr, int len);

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

	rcc_periph_clock_enable(RCC_USART3);
}

static void uart_setup(void)
{
	/* Enable the USART3 interrupt. */
	nvic_enable_irq(NVIC_USART3_IRQ);

	/* setup GPIO: tx on PB10, rx on PB11 */
	//gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
	//	GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART3_TX);
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
		GPIO_CNF_INPUT_FLOAT, GPIO_USART3_RX);

        /* Setup UART parameters. */
	usart_set_baudrate(USART3, 921600);
	usart_set_databits(USART3, 8);
	usart_set_stopbits(USART3, USART_STOPBITS_1);
	usart_set_parity(USART3, USART_PARITY_NONE);
	usart_set_flow_control(USART3, USART_FLOWCONTROL_NONE);
	usart_set_mode(USART3, USART_MODE_RX); // no tx for now

	/* Enable USART3 Receive interrupt. */
	USART_CR1(USART3) |= USART_CR1_RXNEIE;

	/* Finally enable the USART. */
	usart_enable(USART3);
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

static void handle_cmd(uint8_t cmd) {
	static enum {
		STATE_WAITING,
		STATE_GOT_ID1,
		STATE_GOT_ID2,
		STATE_GOT_COL1,
		STATE_GOT_COL2,
		STATE_GOT_COL3,
		STATE_GOT_COL4,
		STATE_GOT_COL5,
	} cmd_state = STATE_WAITING;
	static uint16_t id = 0;
	static uint16_t col_r = 0;
	static uint16_t col_g = 0;
	static uint16_t col_b = 0;

	gpio_toggle(GPIOC, GPIO13);	/* LED on/off */

	switch(cmd_state) {
	case STATE_WAITING:
		id = cmd;
		/* send a bunch of 0x00 to re-sync protocol */
		if(id != 0x00)
			cmd_state = STATE_GOT_ID1;
		break;
	case STATE_GOT_ID1:
		id = (id << 8) + cmd;
		cmd_state = STATE_GOT_ID2;
		break;
	case STATE_GOT_ID2:
		col_r = cmd;
		cmd_state = STATE_GOT_COL1;
		break;
	case STATE_GOT_COL1:
		col_r = (col_r << 8) + cmd;
		cmd_state = STATE_GOT_COL2;
		break;
	case STATE_GOT_COL2:
		col_g = cmd;
		cmd_state = STATE_GOT_COL3;
		break;
	case STATE_GOT_COL3:
		col_g = (col_g << 8) + cmd;
		cmd_state = STATE_GOT_COL4;
		break;
	case STATE_GOT_COL4:
		col_b = cmd;
		cmd_state = STATE_GOT_COL5;
		break;
	case STATE_GOT_COL5:
		col_b = (col_b << 8) + cmd;
		switch(id) {
		case ID_OFFSET:
			timer_set_oc_value(TIM2, TIM_OC1, col_r);
			timer_set_oc_value(TIM3, TIM_OC1, col_g);
			timer_set_oc_value(TIM4, TIM_OC1, col_b);
			break;
		case ID_OFFSET+1:
			timer_set_oc_value(TIM2, TIM_OC2, col_r);
			timer_set_oc_value(TIM3, TIM_OC2, col_g);
			timer_set_oc_value(TIM4, TIM_OC2, col_b);
			break;
		case ID_OFFSET+2:
			timer_set_oc_value(TIM2, TIM_OC3, col_r);
			timer_set_oc_value(TIM3, TIM_OC3, col_g);
			timer_set_oc_value(TIM4, TIM_OC3, col_b);
			break;
		case ID_OFFSET+3:
			timer_set_oc_value(TIM2, TIM_OC4, col_r);
			timer_set_oc_value(TIM3, TIM_OC4, col_g);
			timer_set_oc_value(TIM4, TIM_OC4, col_b);
			break;
		}
		cmd_state = STATE_WAITING;
		break;
	}
}

void usart3_isr(void)
{
        /* Check if we were called because of RXNE. */
        if ((USART_SR(USART3) & USART_SR_RXNE) != 0) {
                handle_cmd(usart_recv(USART3));
        }
}

static const struct usb_device_descriptor dev = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = USB_CLASS_CDC,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,
	.idVendor = 0x0483,
	.idProduct = 0x5740,
	.bcdDevice = 0x0200,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1,
};

/*
 * This notification endpoint isn't implemented. According to CDC spec its
 * optional, but its absence causes a NULL pointer dereference in Linux
 * cdc_acm driver.
 */
static const struct usb_endpoint_descriptor comm_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x83,
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
	.wMaxPacketSize = 16,
	.bInterval = 255,
}};

static const struct usb_endpoint_descriptor data_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x01,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 1,
}, {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x82,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 1,
}};

static const struct {
	struct usb_cdc_header_descriptor header;
	struct usb_cdc_call_management_descriptor call_mgmt;
	struct usb_cdc_acm_descriptor acm;
	struct usb_cdc_union_descriptor cdc_union;
} __attribute__((packed)) cdcacm_functional_descriptors = {
	.header = {
		.bFunctionLength = sizeof(struct usb_cdc_header_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_HEADER,
		.bcdCDC = 0x0110,
	},
	.call_mgmt = {
		.bFunctionLength = 
			sizeof(struct usb_cdc_call_management_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
		.bmCapabilities = 0,
		.bDataInterface = 1,
	},
	.acm = {
		.bFunctionLength = sizeof(struct usb_cdc_acm_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_ACM,
		.bmCapabilities = 0,
	},
	.cdc_union = {
		.bFunctionLength = sizeof(struct usb_cdc_union_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_UNION,
		.bControlInterface = 0,
		.bSubordinateInterface0 = 1, 
	 }
};

static const struct usb_interface_descriptor comm_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_CDC,
	.bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol = USB_CDC_PROTOCOL_AT,
	.iInterface = 0,

	.endpoint = comm_endp,

	.extra = &cdcacm_functional_descriptors,
	.extralen = sizeof(cdcacm_functional_descriptors)
}};

static const struct usb_interface_descriptor data_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 1,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_DATA,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,

	.endpoint = data_endp,
}};

static const struct usb_interface ifaces[] = {{
	.num_altsetting = 1,
	.altsetting = comm_iface,
}, {
	.num_altsetting = 1,
	.altsetting = data_iface,
}};

static const struct usb_config_descriptor config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
	.bNumInterfaces = 2,
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = 0x80,
	.bMaxPower = 0x32,

	.interface = ifaces,
};

static const char *usb_strings[] = {
	"Black Sphere Technologies",
	"CDC-ACM Demo",
	"DEMO",
};

/* Buffer to be used for control requests. */
uint8_t usbd_control_buffer[128];

static int cdcacm_control_request(usbd_device *usbd_dev, struct usb_setup_data *req, uint8_t **buf,
		uint16_t *len, void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{
	(void)complete;
	(void)buf;
	(void)usbd_dev;

	switch(req->bRequest) {
	case USB_CDC_REQ_SET_CONTROL_LINE_STATE: {
		/*
		 * This Linux cdc_acm driver requires this to be implemented
		 * even though it's optional in the CDC spec, and we don't
		 * advertise it in the ACM functional descriptor.
		 */
		char local_buf[10];
		struct usb_cdc_notification *notif = (void *)local_buf;

		/* We echo signals back to host as notification. */
		notif->bmRequestType = 0xA1;
		notif->bNotification = USB_CDC_NOTIFY_SERIAL_STATE;
		notif->wValue = 0;
		notif->wIndex = 0;
		notif->wLength = 2;
		local_buf[8] = req->wValue & 3;
		local_buf[9] = 0;
		// usbd_ep_write_packet(0x83, buf, 10);
		return 1;
		}
	case USB_CDC_REQ_SET_LINE_CODING: 
		if(*len < sizeof(struct usb_cdc_line_coding))
			return 0;

		return 1;
	}
	return 0;
}

static void cdcacm_data_rx_cb(usbd_device *usbd_dev, uint8_t ep)
{
	(void)ep;

	uint8_t buf[64];
	int len = usbd_ep_read_packet(usbd_dev, 0x01, buf, 64);

	for(int i=0; i<len; i++) {
		handle_cmd(buf[i]);
	}
}

static void cdcacm_set_config(usbd_device *usbd_dev, uint16_t wValue)
{
	(void)wValue;

	usbd_ep_setup(usbd_dev, 0x01, USB_ENDPOINT_ATTR_BULK, 64, cdcacm_data_rx_cb);
	usbd_ep_setup(usbd_dev, 0x82, USB_ENDPOINT_ATTR_BULK, 64, NULL);
	usbd_ep_setup(usbd_dev, 0x83, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);

	usbd_register_control_callback(
				usbd_dev,
				USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				cdcacm_control_request);
}

int main(void)
{
	usbd_device *usbd_dev;

	clock_setup();

	// LED
	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);

	uart_setup();

	pwm_setup();

	usbd_dev = usbd_init(&st_usbfs_v1_usb_driver, &dev, &config, usb_strings, 3, usbd_control_buffer, sizeof(usbd_control_buffer));
	usbd_register_set_config_callback(usbd_dev, cdcacm_set_config);

	while (1) {
		__asm__("wfe");
		usbd_poll(usbd_dev);
	}
}
