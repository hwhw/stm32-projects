#ifndef _LCOMM_H_
#define _LCOMM_H_
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "lbus_data.h"

#ifndef LBUS_API
#  if __GNUC__ >= 4
#    define LBUS_API __attribute__((visibility("default")))
#  else
#    define LBUS_API
#  endif
#endif

#define LBUS_GENERIC_ERROR -1
#define LBUS_MEMORY_ERROR -2
#define LBUS_LIBUSB_INIT_ERROR -3
#define LBUS_DEVICE_NOT_FOUND -4
#define LBUS_INTERFACE_NOT_AVAILABLE -5
#define LBUS_BUS_ERROR -6
#define LBUS_NO_ANSWER -7
#define LBUS_BROKEN_ANSWER -8
#define LBUS_MISUSE_ERROR -9
#define LBUS_CRC_ERROR -10
#define LBUS_FIRMWARE_FILE_ERROR -11

struct lbus_ctx_s;
typedef struct lbus_ctx_s lbus_ctx;

/* ========= general API: ======= */

/* connect to USB busmaster
 *
 * \param C pointer to a pointer (yes, this) to a lbus_ctx
 * \return 0 if successful, error code otherwise
 */
int lbus_open(lbus_ctx **C);
/* disconnect from USB busmaster, free lbus_ctx context data
 *
 * \param C lbus_ctx pointer
 */
void lbus_free(lbus_ctx* C);

/* perform a basic communication test with USB busmaster
 *
 * \param C lbus_ctx pointer
 * \return 0 if successful, error code otherwise
 */
int lbus_busmaster_echo(lbus_ctx* C);

/* get error string for error code
 *
 * \param ret error code
 * \return error string if ret contains valid error code, NULL otherwise
 */
const char* lbus_strerror(int ret);


/* ========== high level API: ========== */

/* set PWM configuration
 *
 * \param C lbus_ctx pointer
 * \param dst destination to send message to
 * \param led offset of first PWM channel to write data to
 *        (note that R,G,B are one channel each!)
 * \param vcount number of values in values array
 * \param values actual values to configure
 * \return >=0 if successful, error code otherwise
 */
int lbus_led_set_16bit(lbus_ctx* C, const int dst, const uint16_t led, const unsigned int vcount, const uint16_t values[]);
/* issue command to have slave commit the configured PWM values to its output
 *
 * \param C lbus_ctx pointer
 * \param dst destination to send message to
 * \return >=0 if successful, error code otherwise
 */
int lbus_led_commit(lbus_ctx* C, const int dst);
/* send a ping message
 *
 * \param C lbus_ctx pointer
 * \param dst destination to send message to
 * \return >=0 if successful (ping return value from slave device), error code otherwise
 */
int lbus_ping(lbus_ctx* C, const int dst);
/* send a request to reset slave to bootloader mode
 *
 * \param C lbus_ctx pointer
 * \param dst destination to send message to
 * \return >=0 if successful, error code otherwise
 */
int lbus_reset_to_bootloader(lbus_ctx* C, const int dst);
/* send a request to reset slave to firmware mode
 *
 * \param C lbus_ctx pointer
 * \param dst destination to send message to
 * \return >=0 if successful, error code otherwise
 */
int lbus_reset_to_firmware(lbus_ctx* C, const int dst);
/* send a request to reset slave's configuration store (factory reset)
 *
 * \param C lbus_ctx pointer
 * \param dst destination to send message to
 * \return >=0 if successful, error code otherwise
 */
int lbus_erase_config(lbus_ctx* C, const int dst);
/* query system configuration data from a slave
 *
 * \param C lbus_ctx pointer
 * \param dst destination to send message to
 * \param type config type ID
 * \param numeric true if a numerical answer is expected, false for a string
 * \param answer_bytes number of bytes in the answer
 * \param data for uint32_t numbers (numeric=true,answer_bytes=4)
 *        a pointer to uint32_t, pointer to a buffer for numeric=false,
 *        ignored otherwise
 * \return >=0 if successful (if numeric and answer_bytes=1 or =2,
 *         the number returned), error code otherwise
 */
int lbus_get_config(lbus_ctx* C, const int dst, const uint16_t type, const bool numeric, const int answer_bytes, void* data);
/* configure slave's address
 *
 * \param C lbus_ctx pointer
 * \param dst destination to send message to
 * \param address new address to configure for slave device
 * \return >=0 if successful, error code otherwise
 */
int lbus_set_address(lbus_ctx* C, const int dst, const uint8_t address);
/* configure slave's LED PWM polarity
 *
 * \param C lbus_ctx pointer
 * \param dst destination to send message to
 * \param polarity new polarity to configure for slave device
 * \return >=0 if successful, error code otherwise
 */
int lbus_set_polarity(lbus_ctx* C, const int dst, const uint8_t polarity);
/* read from slave's memory
 *
 * \param C lbus_ctx pointer
 * \param dst destination to send message to
 * \param address memory pointer
 * \param length number of bytes to read from slave (<=1024)
 * \param buf destination buffer to write bytes to
 * \return >=0 if successful, error code otherwise
 */
int lbus_read_memory(lbus_ctx* C, const int dst, const uint32_t address, const int length, void *buf);
/* write firmware to slave (in bootloader mode)
 *
 * \param C lbus_ctx pointer
 * \param dst destination to send message to
 * \param path path of firmware file to write
 * \return >=0 if successful, error code otherwise
 */
int lbus_flash_firmware(lbus_ctx* C, const int dst, const char *path);


/* ========= low-level API: ========= */

/* send data over LBUS
 *
 * \param C lbus_ctx pointer
 * \param buf pointer to data to send
 * \param size size of data to send
 * \return >=0 if successful, error code otherwise
 */
int lbus_tx(lbus_ctx* C, const void* buf, const int size);
/* receive data over LBUS
 *
 * \param C lbus_ctx pointer
 * \param buf pointer to buffer for received data
 * \param size size of data we expect
 * \return number of bytes received, error code otherwise
 */
int lbus_rx(lbus_ctx* C, void* buf, const int size);

#ifdef __cplusplus
}
#endif
#endif // _LCOMM_H_
