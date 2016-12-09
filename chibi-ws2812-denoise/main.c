#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ch.h"
#include "hal.h"

#include "chprintf.h"

#include "usbdescriptor.h"

/***************** USB *********************/
void dataReceived(USBDriver *usbp, usbep_t ep);

uint8_t usb_buf_rx[64];
uint8_t usb_buf_tx[64];
static USBInEndpointState ep1instate;
static const USBEndpointConfig ep1config = {
  USB_EP_MODE_TYPE_BULK,    //Type and mode of the endpoint
  NULL,                     //Setup packet notification callback (NULL for non-control EPs)
  NULL,                     //IN endpoint notification callback
  NULL,                     //OUT endpoint notification callback
  IN_PACKETSIZE,            //IN endpoint maximum packet size
  0x0000,                   //OUT endpoint maximum packet size
  &ep1instate,              //USBEndpointState associated to the IN endpoint
  NULL,                     //USBEndpointState associated to the OUTendpoint
  1,
  NULL                      //Pointer to a buffer for setup packets (NULL for non-control EPs)
};

static USBOutEndpointState ep2outstate;
static const USBEndpointConfig ep2config = {
  USB_EP_MODE_TYPE_BULK,
  NULL,
  NULL,
  dataReceived,
  0x0000,
  OUT_PACKETSIZE,
  NULL,
  &ep2outstate,
  1,
  NULL
};

uint8_t initUSB=0;
static void usb_event(USBDriver *usbp, usbevent_t event) {
  (void) usbp;
  switch (event) {
  case USB_EVENT_RESET:
    //with usbStatus==0 no new transfers will be initiated
    //usbStatus = 0;
    return;
  case USB_EVENT_ADDRESS:
    return;
  case USB_EVENT_CONFIGURED:

    /* Enables the endpoints specified into the configuration.
       Note, this callback is invoked from an ISR so I-Class functions
       must be used.*/
    chSysLockFromISR();
    usbInitEndpointI(usbp, 1, &ep1config);
    usbInitEndpointI(usbp, 2, &ep2config);
    chSysUnlockFromISR();
    //allow the main thread to init the transfers
    initUSB =1;
    return;
  case USB_EVENT_SUSPEND:
    return;
  case USB_EVENT_WAKEUP:
    return;
  case USB_EVENT_STALLED:
    return;
  case USB_EVENT_UNCONFIGURED:
    return;
  }
}

static const USBDescriptor *get_descriptor(USBDriver *usbp, uint8_t dtype, uint8_t dindex, uint16_t lang) {
  (void)usbp;
  (void)lang;

  switch (dtype) {
  case USB_DESCRIPTOR_DEVICE:
    return &usb_device_descriptor;
  case USB_DESCRIPTOR_CONFIGURATION:
    return &usb_configuration_descriptor;
  case USB_DESCRIPTOR_STRING:
    if (dindex < 4){
      return &usb_strings[dindex];
    } else{
      return &usb_strings[4];
    }
  }
  return NULL;
}

/*
bool_t requestsHook(USBDriver *usbp) {
    (void) usbp;
    return FALSE;
}
*/
const USBConfig usbcfg = {
  usb_event,
  get_descriptor,
  /* requestsHook */ NULL,
  NULL
};

/************************* LED & measurement ************************/

/* TIM3, CH4: pin PB1 */
#define WS2812_PWMD PWMD3
#define WS2812_PWM_CH 3
#define WS2812_DMA_STREAM STM32_DMA1_STREAM3
#define WS2812_PORT GPIOB
#define WS2812_PIN 1

#define WS2812_PWM_FREQ 72000000

/* 1.25 us per bit: */
#define WS2812_PERIOD 90

/*
 * for one full frame, we need to send 320*24=7680 bits.
 */
#define WS2812_LEDS_PER_HALFBUF 40
#define WS2812_BUF_LEN (2*WS2812_LEDS_PER_HALFBUF*24)
#define WS2812_LEDS 320

#define ZERO_DUTY 27
#define ONE_DUTY 66

static uint16_t measure[12*8];
static int measure_group = 0;
static uint8_t ledbuffers[WS2812_LEDS*3*2];
static int led_active = 0;
static int led_active_w = 1;
static volatile bool led_toggle = false;
static uint8_t ws2812buf[WS2812_BUF_LEN];
static int nextled = 0;

static const PWMConfig ws2812pwm = {
    WS2812_PWM_FREQ, WS2812_PERIOD, NULL,
    {
      {PWM_OUTPUT_DISABLED, NULL },
      {PWM_OUTPUT_DISABLED, NULL },
      {PWM_OUTPUT_DISABLED, NULL },
      {PWM_OUTPUT_ACTIVE_HIGH, NULL },
    },
    0, 0
};

static const uint16_t colpin[12][2] = {
  {0, 1<<9}, {0, 1<<8}, {0, 1<<7}, {0, 1<<6}, {0, 1<<4}, {0, 1<<3},
  {1<<15, 0}, {1<<8, 0}, {0, 1<<15}, {0, 1<<14}, {0, 1<<13}, {0, 1<<12},
};

void adc_callback(ADCDriver *adcp, adcsample_t *buf, size_t n) {
  (void)adcp;
  (void)buf;
  (void)n;
  /* pull up MOSFET lines */
  palSetPort(GPIOA, 0b1000000100000000); /* clear PA8,15 */
  palSetPort(GPIOB, 0b1111001111011000); /* clear PB3,4,6,7,8,9,12,13,14,15 */
  /* transmit measurement data via USB */
  osalSysLockFromISR();
  if(usbGetDriverStateI(&USBD1) == USB_ACTIVE && !usbGetTransmitStatusI(&USBD1, EP_IN)) {
    /* we just drop all data when USB host isn't receiving fast enough */
    palTogglePad(GPIOC, GPIOC_LED);
    usb_buf_tx[0] = measure_group;
    memcpy(usb_buf_tx+1, buf, 8*2);
    usbStartTransmitI(&USBD1, EP_IN, usb_buf_tx, 1+8*2);
  }
  osalSysUnlockFromISR();
}

void adc_error_cb(ADCDriver *adcp, adcerror_t err) {
  (void)adcp;
  (void)err;
}

static const ADCConversionGroup adcgrpcfg = {
  FALSE, /* non-circular */
  8,     /* 8 channels */
  adc_callback,
  adc_error_cb,
  0, ADC_CR2_TSVREFE,
  ADC_SMPR1_SMP_SENSOR(ADC_SAMPLE_239P5) | ADC_SMPR1_SMP_VREF(ADC_SAMPLE_239P5),
  ADC_SMPR2_SMP_AN0(ADC_SAMPLE_239P5) | ADC_SMPR2_SMP_AN1(ADC_SAMPLE_239P5) | ADC_SMPR2_SMP_AN2(ADC_SAMPLE_239P5) | ADC_SMPR2_SMP_AN3(ADC_SAMPLE_239P5) |
    ADC_SMPR2_SMP_AN4(ADC_SAMPLE_239P5) | ADC_SMPR2_SMP_AN5(ADC_SAMPLE_239P5) | ADC_SMPR2_SMP_AN6(ADC_SAMPLE_239P5) | ADC_SMPR2_SMP_AN7(ADC_SAMPLE_239P5),
  ADC_SQR1_NUM_CH(8),
#if 0
  ADC_SQR2_SQ8_N(ADC_CHANNEL_IN7) | ADC_SQR2_SQ7_N(ADC_CHANNEL_IN6), ADC_SQR3_SQ6_N(ADC_CHANNEL_IN5) | ADC_SQR3_SQ5_N(ADC_CHANNEL_IN4) |
  ADC_SQR3_SQ4_N(ADC_CHANNEL_IN3) | ADC_SQR3_SQ3_N(ADC_CHANNEL_IN2) | ADC_SQR3_SQ2_N(ADC_CHANNEL_IN1) | ADC_SQR3_SQ1_N(ADC_CHANNEL_IN0) 
#else
  ADC_SQR2_SQ8_N(ADC_CHANNEL_IN0) | ADC_SQR2_SQ7_N(ADC_CHANNEL_IN0), ADC_SQR3_SQ6_N(ADC_CHANNEL_IN0) | ADC_SQR3_SQ5_N(ADC_CHANNEL_IN0) |
  ADC_SQR3_SQ4_N(ADC_CHANNEL_IN0) | ADC_SQR3_SQ3_N(ADC_CHANNEL_IN0) | ADC_SQR3_SQ2_N(ADC_CHANNEL_IN0) | ADC_SQR3_SQ1_N(ADC_CHANNEL_IN0) 
#endif
};

void ws2812_fill(uint8_t* buf) {
  static int wait_blank = 0;
  static int measure_state = 0;

  if(nextled == 0) {
    palSetPad(GPIOB, 0);
    chSysLockFromISR();
    adcStartConversionI(&ADCD1, &adcgrpcfg, &measure[measure_state*8], 1);
    chSysUnlockFromISR();
    if(led_toggle) {
      led_toggle = false;
      led_active = 1 - led_active;
    }
  }
  if(nextled == (WS2812_LEDS_PER_HALFBUF*2)) {
    palClearPad(GPIOB, 0);
  }
  
  /* pointer to LED data:
   * be careful with this, it might be an invalid pointer if nextled is
   * not within the range of LEDs buffer is provided for
   */
  uint8_t* d = ledbuffers + 3 * (led_active*WS2812_LEDS + nextled);
  for(int l = WS2812_LEDS_PER_HALFBUF; l > 0; l--) {
    /* this will only work properly when the number of LEDs does not
     * end exactly on the buffer boundary, so make sure it doesn't!
     */
    if(nextled < WS2812_LEDS) {
      for(int i = 0x80; i!=0; i>>=1) *buf++ = (d[0] & i) ? ONE_DUTY : ZERO_DUTY;
      for(int i = 0x80; i!=0; i>>=1) *buf++ = (d[1] & i) ? ONE_DUTY : ZERO_DUTY;
      for(int i = 0x80; i!=0; i>>=1) *buf++ = (d[2] & i) ? ONE_DUTY : ZERO_DUTY;
      d+=3;
      nextled++;
    } else {
      /* no more LED data */
      memset(buf, 0, l*3*8);
      nextled += l;
      break;
    }
  }

  /* when all LED data has been sent, start reset/measurement */
  if((nextled >= WS2812_LEDS) && (!wait_blank)) {
    wait_blank = 3;
  }

  /* start ADC after WS2812b reset period */
  if(wait_blank) {
    wait_blank--;
    if(wait_blank == 1) {
      palSetPort(GPIOA, 0b1000000100000000); /* clear PA8,15 */
      palSetPort(GPIOB, 0b1111001111011000); /* clear PB3,4,6,7,8,9,12,13,14,15 */
      palClearPort(GPIOA, colpin[measure_state][0]);
      palClearPort(GPIOB, colpin[measure_state][1]);
    } else if(wait_blank == 0) {
      /* now, a whole bunch of 0s has been xmit. */
      /* save measure_state for data xmit when ADC is done */
      measure_group = measure_state;

      nextled = 0;

      measure_state += 1;
      if(measure_state == 12) {
        measure_state = 0;
      }
    }
  }
}

void ws2812_dma_isr(void *param, uint32_t flags) {
  (void)param;
  if ((flags & STM32_DMA_ISR_TEIF) != 0) {
    /* DMA error - well, whatever. */
  } else {
    if ((flags & STM32_DMA_ISR_TCIF) != 0) {
      /* prepare DMA data */
      ws2812_fill(&ws2812buf[WS2812_BUF_LEN/2]);
    } else if ((flags & STM32_DMA_ISR_HTIF) != 0) {
      /* prepare DMA data */
      ws2812_fill(ws2812buf);
    }
  }
}

void run_ws2812(void) {
  bool b;

  pwmStart(&WS2812_PWMD, &ws2812pwm);

  b = dmaStreamAllocate(WS2812_DMA_STREAM, 10, (stm32_dmaisr_t)ws2812_dma_isr, NULL);
  osalDbgAssert(!b, "stream already allocated");

  dmaStreamSetPeripheral(WS2812_DMA_STREAM, &(WS2812_PWMD.tim->CCR[WS2812_PWM_CH]));
  dmaStreamSetMemory0(WS2812_DMA_STREAM, ws2812buf);
  dmaStreamSetTransactionSize(WS2812_DMA_STREAM, WS2812_BUF_LEN);
  dmaStreamSetMode(
      WS2812_DMA_STREAM,
      STM32_DMA_CR_DIR_M2P |
      STM32_DMA_CR_MSIZE_BYTE | STM32_DMA_CR_MINC |
      STM32_DMA_CR_PSIZE_HWORD |
      STM32_DMA_CR_CIRC |
      STM32_DMA_CR_PL(2) |
      STM32_DMA_CR_HTIE | STM32_DMA_CR_TCIE | STM32_DMA_CR_TEIE);
  pwmEnableChannel(&WS2812_PWMD, WS2812_PWM_CH, 0);
  dmaStreamEnable(WS2812_DMA_STREAM);
  WS2812_PWMD.tim->DIER |= STM32_TIM_DIER_CC1DE<<WS2812_PWM_CH;
}


/*===========================================================================*/
/* Generic code.                                                             */
/*===========================================================================*/

void dataReceived(USBDriver *usbp, usbep_t ep) {
  USBOutEndpointState *osp = usbp->epc[ep]->out_state;
  (void) usbp;
  (void) ep;

  if(osp->rxcnt >= 2) {
    palTogglePad(GPIOC, GPIOC_LED);
    uint16_t cmd = (usb_buf_rx[0] << 8) | usb_buf_rx[1];
    uint16_t offs = cmd & 0x1FF;
    /* always write to the inactive buffer */
    uint8_t* d = ledbuffers + 3 * (led_active_w*WS2812_LEDS + offs);
    //uint8_t* d = ledbuffers + 3 * (led_active*WS2812_LEDS + offs);
    memcpy(d, usb_buf_rx + 2, osp->rxcnt - 2);
    /* flip buffers */
    if(cmd & 0x8000) {
      led_toggle = true;
      led_active_w = 1 - led_active_w;
    }
  }

  chSysLockFromISR();
  if(usbGetDriverStateI(usbp) == USB_ACTIVE && !usbGetReceiveStatusI(usbp, EP_OUT)) {
    usbStartReceiveI(usbp, EP_OUT, usb_buf_rx, 64);
  }
  chSysUnlockFromISR();
}

/*
 * Blinker thread, times are in milliseconds.
 */
static THD_WORKING_AREA(waThread1, 128);
static __attribute__((noreturn)) THD_FUNCTION(Thread1, arg) {

  (void)arg;
  chRegSetThreadName("blinker");
  while (true) {
    systime_t time = /*serusbcfg.usbp->state == USB_ACTIVE ? 250 :*/ 500;
    palClearPad(GPIOC, GPIOC_LED);
    chThdSleepMilliseconds(time);
    palSetPad(GPIOC, GPIOC_LED);
    chThdSleepMilliseconds(time);
  }
}

int main(void) {
  halInit();
  chSysInit();

  /* PA8,15; PB3,4,6,7,8,9,12,13,14,15: GPIO output (column selector) */
  palSetPort(GPIOA, 0b1000000100000000);
  palSetPort(GPIOB, 0b1111001111011000);
  palSetGroupMode(GPIOA, 0b1000000100000000, 0, PAL_MODE_OUTPUT_PUSHPULL);
  palSetGroupMode(GPIOB, 0b1111001111011000, 0, PAL_MODE_OUTPUT_PUSHPULL);

  /* PA0..PA7: analog input */
  palSetPadMode(GPIOA, 0, PAL_MODE_INPUT_ANALOG);
  palSetPadMode(GPIOA, 1, PAL_MODE_INPUT_ANALOG);
  palSetPadMode(GPIOA, 2, PAL_MODE_INPUT_ANALOG);
  palSetPadMode(GPIOA, 3, PAL_MODE_INPUT_ANALOG);
  palSetPadMode(GPIOA, 4, PAL_MODE_INPUT_ANALOG);
  palSetPadMode(GPIOA, 5, PAL_MODE_INPUT_ANALOG);
  palSetPadMode(GPIOA, 6, PAL_MODE_INPUT_ANALOG);
  palSetPadMode(GPIOA, 7, PAL_MODE_INPUT_ANALOG);

  /* enable clock etc. for ADC1 */
  adcStart(&ADCD1, NULL);

  /* PB1: WS2812b data out */
  palSetPadMode(WS2812_PORT, WS2812_PIN, PAL_MODE_STM32_ALTERNATE_PUSHPULL);

  palSetPadMode(GPIOB, 0, PAL_MODE_OUTPUT_PUSHPULL);

  /* USB interface */
  usbStart(&USBD1, &usbcfg);

  //chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO, Thread1, NULL);

  run_ws2812();

  while (true) {
    chSysLock();
    if(usbGetDriverStateI(&USBD1) == USB_ACTIVE && !usbGetReceiveStatusI(&USBD1, EP_OUT)) {
      usbStartReceiveI(&USBD1, EP_OUT, usb_buf_rx, 64);
    }
    chSysUnlock();
    chThdSleepMilliseconds(100);
  }
}
