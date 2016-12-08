#ifndef _WS2812_H
#define _WS2812_H

#define WS2812_PWM_FREQ 72000000
#define WS2812_PERIOD 90
#define WS2812_RESET_CYCLES 40
#define WS2812_DUTY_BUFFER_LEN(pixels) (24*(pixels) + WS2812_RESET_CYCLES)
#define WS2812_DUTY_BUFFER(pixels,name) uint8_t name[WS2812_DUTY_BUFFER_LEN(pixels)]

typedef struct {
  PWMDriver* pwmd;
  pwmchannel_t ch;
  const stm32_dma_stream_t* dma_stream;
  int len;
  uint8_t *buf;
  binary_semaphore_t *sem;
} WS2812Config;

inline static void ws2812Aquire(const WS2812Config *cfg) { chBSemWait(cfg->sem); }
inline static void ws2812Release(const WS2812Config *cfg) { chBSemSignal(cfg->sem); }

void ws2812Init(const WS2812Config* cfg, const PWMConfig* pwm_cfg);
void ws2812Send(const WS2812Config* cfg);
void ws2812Set(const WS2812Config* cfg, int px, uint8_t r, uint8_t g, uint8_t b);
void ws2812Get(const WS2812Config* cfg, int px, uint8_t* r, uint8_t* g, uint8_t *b);

#endif // _WS2812_H
