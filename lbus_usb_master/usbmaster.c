#include <string.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/dfu.h>

#include "../lbus_common/lbus.h"

uint8_t usbd_control_buffer[128];

const struct usb_device_descriptor dev = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = 0,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,
	.idVendor = 0xDEAD,
	.idProduct = 0xCAFE,
	.bcdDevice = 0x0100,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1,
};

const struct usb_endpoint_descriptor ep_bulk[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x01,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 0,
}, {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x82,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 0,
}};

const struct usb_interface_descriptor iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = 0xFF,
	.bInterfaceSubClass = 0xFF,
	.bInterfaceProtocol = 0xFF,

	.iInterface = 0,

	.endpoint = ep_bulk,
};

const struct usb_interface ifaces[] = {{
	.num_altsetting = 1,
	.altsetting = &iface,
}};

const struct usb_config_descriptor config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
	.bNumInterfaces = 1,
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = 0x80,
	.bMaxPower = 50, // 100 mA

	.interface = ifaces,
};

static const char *usb_strings[] = {
	"HW",
	"LightBus Master",
	"1.0",
};

#define RECV_SIZE 2048
uint8_t recv_buf[RECV_SIZE];
volatile int recv_head=0;
volatile int recv_tail=0;

#define CMD_XMIT 1
#define CMD_RECV 2
#define CMD_ECHO 3
static void usbmaster_receive_cb(usbd_device *usbd_dev, uint8_t ep) {
	(void)ep;
	(void)usbd_dev;

	uint8_t buf[64]  __attribute__ ((aligned(4)));
	int len = 0;

	len = usbd_ep_read_packet(usbd_dev, 0x01, buf, 64);
	if(len < 1) return;
	const uint8_t cmd = buf[0];

	if(cmd == CMD_XMIT) {
		//USART_CR1(LBUS_USART) &= ~USART_CR1_RE;
		gpio_set(LBUS_RE_GPIO, LBUS_RE_PIN);
		gpio_set(LBUS_DE_GPIO, LBUS_DE_PIN);
		for(int i=1; i<len; i++) {
			usart_send_blocking(LBUS_USART, buf[i]);
		}
		while((USART_SR(LBUS_USART) & USART_SR_TC) == 0) __asm("nop");
		recv_tail = recv_head; // drop current RX queue contents
		gpio_clear(LBUS_DE_GPIO, LBUS_DE_PIN);
		gpio_clear(LBUS_RE_GPIO, LBUS_RE_PIN);
		//USART_CR1(LBUS_USART) |= USART_CR1_RE;
	} else if(cmd == CMD_RECV) {
		if(len < 2) return;
		const uint8_t size = buf[1];
		TIM1_CNT = 0;
		TIM_CR1(TIM1) |= TIM_CR1_CEN;
		int i = 0;
		while(i<size) {
			if(recv_head != recv_tail) {
				buf[i++] = recv_buf[recv_tail++];
				recv_tail = recv_tail % RECV_SIZE;
			}
			if(TIM1_CNT > 3+LBUS_TIMEOUT) {
				break;
			}
		}
		usbd_ep_write_packet(usbd_dev, 0x82, buf, i);
		TIM_CR1(TIM1) &= ~TIM_CR1_CEN;
	} else if(cmd == CMD_ECHO) {
		usbd_ep_write_packet(usbd_dev, 0x82, buf+1, len-1);
	}
}

static void usbmaster_set_config(usbd_device *usbd_dev, uint16_t wValue) {
	(void)wValue;

	usbd_ep_setup(usbd_dev, 0x01, USB_ENDPOINT_ATTR_BULK, 64, usbmaster_receive_cb);
	usbd_ep_setup(usbd_dev, 0x82, USB_ENDPOINT_ATTR_BULK, 64, NULL);
}

static usbd_device *usbd_dev;

void usb_lp_can_rx0_isr(void) {
	usbd_poll(usbd_dev);
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
		recv_buf[recv_head++] = usart_recv(LBUS_USART);
		recv_head = recv_head % RECV_SIZE;
	}
}

int main(void) {
	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	RCC_APB2ENR |= (RCC_APB2ENR_USART1EN | RCC_APB2ENR_TIM1EN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN);
	RCC_APB1ENR |= (RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN | RCC_APB1ENR_USART3EN | RCC_APB1ENR_USART2EN | RCC_APB1ENR_TIM4EN | RCC_APB1ENR_TIM3EN | RCC_APB1ENR_TIM2EN);

	/* Pin setup */
	gpio_set_mode(LBUS_USART_GPIO, GPIO_MODE_OUTPUT_50_MHZ,
		GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, LBUS_USART_GPIO_TX);
	gpio_set_mode(LBUS_USART_GPIO, GPIO_MODE_INPUT,
		GPIO_CNF_INPUT_FLOAT, LBUS_USART_GPIO_RX);
	gpio_set_mode(LBUS_DE_GPIO, GPIO_MODE_OUTPUT_50_MHZ,
		GPIO_CNF_OUTPUT_PUSHPULL, LBUS_DE_PIN);
	gpio_set_mode(LBUS_RE_GPIO, GPIO_MODE_OUTPUT_50_MHZ,
		GPIO_CNF_OUTPUT_PUSHPULL, LBUS_RE_PIN);

	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);

	gpio_set(LBUS_RE_GPIO, LBUS_RE_PIN);
	gpio_set(LBUS_DE_GPIO, LBUS_DE_PIN);

	/* Setup UART parameters. */
	usart_set_baudrate(LBUS_USART, LBUS_BAUDRATE);

	/* empty USART receive buffer */
	while((USART_SR(LBUS_USART) & USART_SR_RXNE) != 0)
		usart_recv(LBUS_USART);

	/* Enable the USART interrupt. */
	nvic_enable_irq(LBUS_USART_IRQ);

	/* configure and enable USART. */
	USART_CR1(LBUS_USART) |= USART_CR1_RXNEIE | USART_CR1_RE | USART_CR1_TE | USART_CR1_UE;

	/* set up a timer for timing out packet receives: */
	RCC_APB2RSTR |= RCC_APB2RSTR_TIM1RST;
	RCC_APB2RSTR &= ~RCC_APB2RSTR_TIM1RST;
	TIM_PSC(TIM1) = ((CPU_SPEED/2)/10000); // 1 tick = 100 usec
	TIM_EGR(TIM1) |= TIM_EGR_UG;

	/* set up USB driver */
	usbd_dev = usbd_init(&st_usbfs_v1_usb_driver, &dev, &config, usb_strings, 3, usbd_control_buffer, sizeof(usbd_control_buffer));
	usbd_register_set_config_callback(usbd_dev, usbmaster_set_config);

	nvic_set_priority(NVIC_USB_LP_CAN_RX0_IRQ, (1 << 4));
	nvic_enable_irq(NVIC_USB_LP_CAN_RX0_IRQ);

	while (1) {
		//usbd_poll(usbd_dev);
		__asm("wfe");
	}
}
