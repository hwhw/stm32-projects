#include "ch.h"
#include "hal.h"
#include "ws2812.h"
#include "stm32_dma.h"
#include "pwm.h"
#include "string.h"

#define ZERO_DUTY 27
#define ONE_DUTY 66

void ws2812Init(const WS2812Config* cfg, const PWMConfig* pwm_cfg) {
  chBSemObjectInit(cfg->sem, false);
	pwmStart(cfg->pwmd, pwm_cfg);
}

void ws2812Set(const WS2812Config* cfg, int px, uint8_t r, uint8_t g, uint8_t b) {
  uint8_t* d = cfg->buf + (px*24);
  for(int i = 0x80; i!=0; i>>=1) *d++ = (g&i) ? ONE_DUTY : ZERO_DUTY;
  for(int i = 0x80; i!=0; i>>=1) *d++ = (r&i) ? ONE_DUTY : ZERO_DUTY;
  for(int i = 0x80; i!=0; i>>=1) *d++ = (b&i) ? ONE_DUTY : ZERO_DUTY;
}

void ws2812Get(const WS2812Config* cfg, int px, uint8_t* r, uint8_t* g, uint8_t *b) {
  uint8_t* d = cfg->buf + (px*24);
  for(int i = 0x80; i!=0; i>>=1) *g |= (*d++ == ONE_DUTY) ? i : 0;
  for(int i = 0x80; i!=0; i>>=1) *r |= (*d++ == ONE_DUTY) ? i : 0;
  for(int i = 0x80; i!=0; i>>=1) *b |= (*d++ == ONE_DUTY) ? i : 0;
}

void ws2812Send(const WS2812Config* cfg) {
  dmaStreamAllocate(cfg->dma_stream, 10, NULL, NULL);
  dmaStreamSetPeripheral(cfg->dma_stream, &(cfg->pwmd->tim->CCR[cfg->ch]));
  dmaStreamSetMemory0(cfg->dma_stream, cfg->buf);
  dmaStreamSetTransactionSize(cfg->dma_stream, WS2812_DUTY_BUFFER_LEN(cfg->len));
  dmaStreamSetMode(
      cfg->dma_stream,
      STM32_DMA_CR_DIR_M2P | STM32_DMA_CR_MINC | STM32_DMA_CR_PSIZE_HWORD
      | STM32_DMA_CR_MSIZE_BYTE | STM32_DMA_CR_PL(2));
  pwmEnableChannel(cfg->pwmd, cfg->ch, 0);
  dmaStreamEnable(cfg->dma_stream);
  cfg->pwmd->tim->DIER |= STM32_TIM_DIER_CC1DE<<(cfg->ch);
  dmaWaitCompletion(cfg->dma_stream);
  cfg->pwmd->tim->DIER &= ~(STM32_TIM_DIER_CC1DE<<(cfg->ch));
  dmaStreamRelease(cfg->dma_stream);
  pwmDisableChannel(cfg->pwmd, cfg->ch);
}
