#ifndef _LBUS_H_
#define _LBUS_H_

#include <stdint.h>
#include "platform.h"

/* packet receive times out after 1msec (=10 ticks) */
#define LBUS_TIMEOUT 10

#define LBUS_USART USART3
#define LBUS_USART_RCC RCC_USART3
#define LBUS_USART_GPIO_RCC RCC_GPIOB
#define LBUS_USART_IRQ NVIC_USART3_IRQ
#define LBUS_USART_GPIO GPIOB
#define LBUS_USART_GPIO_TX GPIO_USART3_TX
#define LBUS_USART_GPIO_RX GPIO_USART3_RX
#define LBUS_USART_ISR usart3_isr

#define LBUS_BAUDRATE 500000
#define LBUS_DE_GPIO GPIOB
#define LBUS_DE_PIN GPIO12
#define LBUS_RE_GPIO GPIOB
#define LBUS_RE_PIN GPIO13

/* wait 100 us before sending TX part of packet (=1 tick)
 * keep this lower than the timeout above!
 */
#define LBUS_TX_WAIT 1

#include "lbus_data.h"

/* LBUS API */
void lbus_init(void);
void lbus_send(const uint8_t txbyte);
void lbus_send_buf(const void *buf, const int length);
inline void lbus_send32(const uint32_t *txwordptr) { lbus_send_buf(txwordptr, 4); }
void lbus_start_tx(void);
void lbus_end_pkg(void);
void lbus_reset_to_bootloader(void);

/* For receiving packet data (following the packet header), define a matching lbus_recv_func:
 * it will be called for each received byte (rbyte) and has access to the packet header and
 * a counter for the current position within the packet (starting at 1 for the first byte
 * in the packet).
 */
typedef void (*lbus_recv_func)(const uint8_t rbyte, const struct lbus_hdr *header, const unsigned int p);
/* define this in your application to handle LBUS communication: */
extern lbus_recv_func lbus_handler(const struct lbus_hdr *header);

/* convenience macros for defining lbus_recv_funcs: */
#define LBUS_READ_PKG(pkgdef,var,pos,data) \
	pkgdef var; \
	((uint8_t*)&var)[pos-sizeof(struct lbus_hdr)-1] = data;
#define LBUS_IS_COMPLETE(pos,var) \
	(pos==sizeof(struct lbus_hdr)+sizeof(var))
#define LBUS_HANDLE_COMPLETE(pkgdef,var,pos,data) \
	LBUS_READ_PKG(pkgdef,var,pos,data) \
	if(LBUS_IS_COMPLETE(pos,var))

#endif // _LBUS_H
