#define _DEFAULT_SOURCE
#include <SDL.h>
#include <SDL_opengl.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include "usbbb.h"

#define LED_COUNT 320
#define SENSOR_ROWS 8
#define SENSOR_COLS 12

// you can override this using the environment variable BBEMU.
// set it to the number of milliseconds per frame.
#define TIME_PER_FRAME 12

#define M_OVER (1<<31)
#define S_RGB_MASK 0x00FFFFFF
#define S_RGB_RED(x) ((x>>16)&0xFF)
#define S_RGB_GREEN(x) ((x>>8)&0xFF)
#define S_RGB_BLUE(x) (x&0xFF)

struct bb_ctx_s {
  SDL_Thread *event_thread;
  SDL_Window *window;
  SDL_Renderer *renderer;
  uint32_t sensorstate[SENSOR_COLS*SENSOR_ROWS];
  uint32_t chipstate[4];
  uint8_t chipcolor[3];

  uint8_t fb[LED_COUNT*3];
  uint8_t xmit_fb[LED_COUNT*3];
  bool xmit;
  uint16_t sensors[SENSOR_COLS*SENSOR_ROWS];

  bool running;
  bool transmitting;
  int measure_row;
  int xmit_offs;
  int sensor_last_row;
  int ms_per_frame;
  SDL_mutex *mutex_transmitting;
  SDL_mutex *mutex_wait_sensor;
  SDL_cond *cond_wait_sensor;

  uint8_t rbuf[1+SENSOR_ROWS*sizeof(uint16_t)];

  int16_t pos_led[40][40];
  int16_t pos_leds10[10][10][4];
};

BB_API
void bb_get_sensordata(bb_ctx *C, uint16_t sensordata[]) {
  SDL_LockMutex(C->mutex_wait_sensor);
  memcpy(sensordata, C->sensors, sizeof(uint16_t)*SENSOR_COLS*SENSOR_ROWS);
  SDL_UnlockMutex(C->mutex_wait_sensor);
}

static SDL_Rect* rect_led(int led, SDL_Rect* r) {
  int offs = led % 40;
  if(led < 160) {
    /* rows in X-direction */
    r->w = 28;
    r->h = 56;
    if(offs >= 20) {
      offs = 19 - (offs - 20);
    }
    /* don't ask me, I'm just working here: */
    r->x = 20 + 2 + offs*30 - 2*(offs%2);
    r->y = 20 + 62 + (led / 20)*60;
  } else {
    /* columns in Y-direction */
    r->w = 56;
    r->h = 28;
    if(offs >= 20) {
      offs = 19 - (offs - 20);
    }
    r->y = 20 + 572 - (2 + offs*30 - 2*(offs%2));
    r->x = 20 + 62 + ((led-160) / 20)*60;
  }
  return r;
}

static SDL_Rect* rect_sensor(int sensor, SDL_Rect* r, int border) {
  if(sensor < 64) {
    r->w = 56+border*2;
    r->h = 56+border*2;
    int x=sensor/8;
    int y=7-(sensor % 8);
    r->x = 82 - border + x*60;
    r->y = 82 - border + y*60;
  } else if(sensor < 72) {
    r->w = 16+border*2;
    r->h = 56+border*2;
    r->y = 82-border + (sensor-64)*60;
    r->x = 621-border;
  } else if(sensor < 80) {
    r->w = 56+border*2;
    r->h = 16+border*2;
    r->x = 82-border + (sensor-72)*60;
    r->y = 2-border;
  } else if(sensor < 88) {
    r->w = 16+border*2;
    r->h = 56+border*2;
    r->y = 82-border + (sensor-80)*60;
    r->x = 2-border;
  } else if(sensor < 96) {
    r->w = 56+border*2;
    r->h = 16+border*2;
    r->x = 82-border + (sensor-88)*60;
    r->y = 621-border;
  }
  return r;
}

static int sensor_pos(int x, int y) {
  int sensor = -1;
  if(x < 20 && y >= (20+60) && y < (20+540)) {
    // sensors left
    sensor = 80 + (y-(20+60))/60;
  } else if(x >= 620 && y >= (20+60) && y < (20+540)) {
    // sensors right
    sensor = 64 + (y-(20+60))/60;
  } else if(y < 20 && x >= (20+60) && x < (20+540)) {
    // sensors top
    sensor = 72 + (x-(20+60))/60;
  } else if(y >= 620 && x >= (20+60) && x < (20+540)) {
    // sensors bottom
    sensor = 88 + (x-(20+60))/60;
  } else if(x >= 80 && x < 560 && y >= 80 && y < 560) {
    // 8x8 sensor fields
    sensor = ((x-(20+60))/60) * 8 + (7-(y-(20+60))/60);
  }
  return sensor;
}

static SDL_Rect* rect_chip(int chip, SDL_Rect* r, int border) {
  r->w = 25+2*border;
  r->h = 25+2*border;
  switch(chip) {
    case 0:
      r->x = 5-border;
      r->y = 5-border;
      break;
    case 1:
      r->x = 5-border;
      r->y = 34-border;
      break;
    case 2:
      r->x = 34-border;
      r->y = 5-border;
      break;
    case 3:
      r->x = 34-border;
      r->y = 34-border;
      break;
  }
  return r;
}

static int chip_pos(int x, int y) {
  if(x >=5 && x < 30 && y >= 5 && y < 30) {
    return 0;
  } else if(x >=5 && x < 30 && y >= 34 && y < 59) {
    return 1;
  } else if(x >= 34 && x < 59 && y >= 5 && y < 30) {
    return 2;
  } else if(x >= 34 && x < 59 && y >= 34 && y < 59) {
    return 3;
  }
  return -1;
}

static void render(bb_ctx *C) {
  SDL_SetRenderDrawBlendMode(C->renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(C->renderer, 0, 0, 0, 255);
  SDL_RenderClear(C->renderer);
  SDL_Rect r;

  /* paint fields */
  uint8_t *c = C->xmit_fb;
  for(int i=0; i<320; i++) {
    int offs = i % 20;
    if(offs < 2 || offs > 17) {
      /* side fields are a bit darker */
      SDL_SetRenderDrawColor(C->renderer, *c++, *c++, *c++, 200);
    } else {
      SDL_SetRenderDrawColor(C->renderer, *c++, *c++, *c++, (i<160) ? 255 : 128);
    }
    SDL_RenderFillRect(C->renderer, rect_led(i, &r));
  }
  /* paint sensor markers */
  for(int i=0; i<96; i++) {
    uint32_t v = C->sensorstate[i];
    if(v & M_OVER) {
      if(i<64) {
        SDL_SetRenderDrawColor(C->renderer, C->chipcolor[0], C->chipcolor[1], C->chipcolor[2], 255);
      } else {
        SDL_SetRenderDrawColor(C->renderer, 255, 255, 255, 255);
      }
      SDL_RenderDrawRect(C->renderer, rect_sensor(i, &r, 1));
      SDL_RenderDrawRect(C->renderer, rect_sensor(i, &r, 2));
    }
    if(v & S_RGB_MASK) {
      if(i<64) {
        SDL_SetRenderDrawColor(C->renderer, S_RGB_RED(v), S_RGB_GREEN(v), S_RGB_BLUE(v), 255);
        SDL_RenderFillRect(C->renderer, rect_sensor(i, &r, -11));
        SDL_SetRenderDrawColor(C->renderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(C->renderer, rect_sensor(i, &r, -10));
      } else {
        SDL_SetRenderDrawColor(C->renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(C->renderer, rect_sensor(i, &r, 0));
      }
    }
  }
  /* paint chip markers */
  for(int i=0; i<4; i++) {
    switch(i) {
      case 0:
        SDL_SetRenderDrawColor(C->renderer, 200, 0, 0, 255);
        break;
      case 1:
        SDL_SetRenderDrawColor(C->renderer, 0, 200, 0, 255);
        break;
      case 2:
        SDL_SetRenderDrawColor(C->renderer, 0, 0, 200, 255);
        break;
      case 3:
        SDL_SetRenderDrawColor(C->renderer, 150, 0, 150, 255);
        break;
    }
    SDL_RenderFillRect(C->renderer, rect_chip(i, &r, 0));
    if(C->chipstate[i] & M_OVER) {
      SDL_SetRenderDrawColor(C->renderer, 255, 255, 255, 255);
      SDL_RenderDrawRect(C->renderer, rect_chip(i, &r, 1));
    }
  }
  SDL_RenderPresent(C->renderer);
}

static int bb_event_thread(void *d) {
  SDL_Event event;
  bb_ctx *C = (bb_ctx*)d;

	if(NULL == (C->window = SDL_CreateWindow(
          "BB Emulator",
          SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
          640, 640,
          SDL_WINDOW_SHOWN))) {
		return -1;
	}
  if(NULL == (C->renderer = SDL_CreateRenderer(C->window, -1, SDL_RENDERER_ACCELERATED))) {
    return -1;
  }

  C->running = true;
  int next = SDL_GetTicks() + C->ms_per_frame;
  while(C->running) {
    int now = SDL_GetTicks();
    int wait = (next - now);
    if(wait <= 0 || !SDL_WaitEventTimeout(&event, wait)) {
      next += C->ms_per_frame;
      SDL_LockMutex(C->mutex_transmitting);
      if(C->xmit) {
        C->xmit = false;
        render(C);
      }
      
      SDL_LockMutex(C->mutex_wait_sensor);

      for(int i=0; i<SENSOR_ROWS; i++) {
        int s = C->measure_row * SENSOR_ROWS + i;
        if(C->sensorstate[s] & S_RGB_MASK) {
          // TODO: set sensor state according to LED and chip color
        } else {
          int level = 100; // TODO: make this dependent on LED color (leakage)
          C->sensors[s] = level+(random()%120);
        }
      }

      SDL_CondSignal(C->cond_wait_sensor);
      SDL_UnlockMutex(C->mutex_wait_sensor);

      SDL_UnlockMutex(C->mutex_transmitting);
    } else {
      if(event.type == SDL_MOUSEMOTION) {
        int mx = event.motion.x;
        int my = event.motion.y;
        int p = sensor_pos(mx, my);
        if(p != -1) {
          for(int i=0; i<96; i++) C->sensorstate[i] &= ~(M_OVER);
          C->sensorstate[p] |= M_OVER;
        }
      } else if(event.type == SDL_MOUSEBUTTONDOWN) {
        int mx = event.button.x;
        int my = event.button.y;
        int p = sensor_pos(mx, my);
        if(p != -1) {
          switch(event.button.button) {
            case SDL_BUTTON_LEFT:
              C->sensorstate[p] |= (C->chipcolor[0] << 16) | (C->chipcolor[1] << 8) | C->chipcolor[2];
              break;
            case SDL_BUTTON_RIGHT:
              if(C->sensorstate[p] & S_RGB_MASK) {
                C->sensorstate[p] &= ~(S_RGB_MASK);
              } else {
                C->sensorstate[p] |= (C->chipcolor[0] << 16) | (C->chipcolor[1] << 8) | C->chipcolor[2];
              }
              break;
          }
        }
        p = chip_pos(mx, my);
        if(p != -1) {
          for(int i=0; i<4; i++) C->chipstate[i] &= ~(M_OVER);
          C->chipstate[p] |= M_OVER;
          switch(p) {
            case 0:
              C->chipcolor[0] = 255;
              C->chipcolor[1] = 0;
              C->chipcolor[2] = 0;
              break;
            case 1:
              C->chipcolor[0] = 0;
              C->chipcolor[1] = 255;
              C->chipcolor[2] = 0;
              break;
            case 2:
              C->chipcolor[0] = 0;
              C->chipcolor[1] = 0;
              C->chipcolor[2] = 255;
              break;
            case 3:
              C->chipcolor[0] = 200;
              C->chipcolor[1] = 0;
              C->chipcolor[2] = 200;
              break;
          }
        }
      } else if(event.type == SDL_MOUSEBUTTONUP) {
        int mx = event.button.x;
        int my = event.button.y;
        int p = sensor_pos(mx, my);
        if(p != -1) {
          switch(event.button.button) {
            case SDL_BUTTON_LEFT:
              C->sensorstate[p] &= ~(S_RGB_MASK);
              break;
          }
        }
      }
    }
  }
}

void bb_init_pos_led(bb_ctx* C) {
  memset(C->pos_led, 0xFF, 40*40*sizeof(int16_t));
  memset(C->pos_leds10, 0xFF, 10*10*4*sizeof(int16_t));
  for(int i=0; i<LED_COUNT; i++) {
    int x, y;
    bb_get_led_pos(C, i, &x, &y);

    C->pos_led[y][x] = i;

    int16_t *p10 = C->pos_leds10[y>>2][x>>2];
    while(*p10 != -1) p10++;
    *p10 = i;
  }
}

BB_API
int bb_open(bb_ctx **C) {
	*C = calloc(1, sizeof(bb_ctx));
	if(*C == NULL)
		return BB_MEMORY_ERROR;

  (*C)->mutex_wait_sensor = SDL_CreateMutex();
  if((*C)->mutex_wait_sensor == NULL)
		return BB_MEMORY_ERROR;
  (*C)->cond_wait_sensor = SDL_CreateCond();
  if((*C)->cond_wait_sensor == NULL)
		return BB_MEMORY_ERROR;
  (*C)->mutex_transmitting = SDL_CreateMutex();
  if((*C)->mutex_transmitting == NULL)
		return BB_MEMORY_ERROR;

  bb_init_pos_led(*C);
  (*C)->chipcolor[0] = 255;

  char *bbemu = getenv("BBEMU");
  if(bbemu == NULL) {
    (*C)->ms_per_frame = TIME_PER_FRAME;
  } else {
    (*C)->ms_per_frame = strtoul(bbemu, NULL, 0);
    if((*C)->ms_per_frame == 0)
      (*C)->ms_per_frame = TIME_PER_FRAME;
  }

  if(NULL == ((*C)->event_thread = SDL_CreateThread(bb_event_thread, "Event Thread", (void*) *C))) {
    return BB_POLL_ERROR;
  }
	return 0;
}

BB_API
void bb_free(bb_ctx* C) {
	if(C != NULL) {
    if(C->running) {
      C->running = false;
      int status;
      SDL_WaitThread(C->event_thread, &status);
      (void)status;
      SDL_DestroyMutex(C->mutex_transmitting);
      SDL_DestroyMutex(C->mutex_wait_sensor);
      SDL_DestroyCond(C->cond_wait_sensor);
    }
		if(C->window != NULL) {
      SDL_DestroyWindow(C->window);
		}
		free(C);
	}
}

BB_API
int bb_transmit(bb_ctx *C, int measure_row) {
  SDL_LockMutex(C->mutex_transmitting);
  C->xmit = true;
  memcpy(C->xmit_fb, C->fb, LED_COUNT*3);
  C->measure_row = (measure_row == -1) ? ((C->measure_row + 1)%12) : measure_row;
  SDL_UnlockMutex(C->mutex_transmitting);
  return 0;
}

BB_API
void bb_get_led_pos(bb_ctx* C, int led, int* x, int* y) {
  if(led < 160) {
    // left-right
    *y = 5 + 4 * (led / 20);
    *x = 2 * (led % 20);
    if(((led / 20) % 2) == 1)
      *x = 38 - *x;
  } else {
    // down-up
    led -= 160;
    *x = 5 + 4 * (led / 20);
    *y = 2 * (led % 20);
    if(((led / 20) % 2) == 0)
      *y = 38 - *y;
  }
}

BB_API
void bb_set_led(bb_ctx *C, const int led, const uint8_t r, const uint8_t g, const uint8_t b) {
  uint8_t *p = C->fb + led*3;
  *p++ = g;
  *p++ = r;
  *p = b;
}

BB_API
void bb_set_led10(bb_ctx* C, const int x, const int y, const int r, const int g, const int b) {
  for(int i=0; i<4; i++) {
    int led = C->pos_leds10[y][x][i];
    if(led == -1) break;
    bb_set_led(C, led, r, g, b);
  }
}

BB_API
void bb_set_led40(bb_ctx* C, const int x, const int y, const int r, const int g, const int b) {
  int led = C->pos_led[y][x];
  if(led != -1)
    bb_set_led(C, led, r, g, b);
}

BB_API
int bb_wait_measure(bb_ctx* C) {
  SDL_LockMutex(C->mutex_wait_sensor);
  while(0 != SDL_CondWait(C->cond_wait_sensor, C->mutex_wait_sensor)) {
    // loop
  }
  int r = C->sensor_last_row;
  SDL_UnlockMutex(C->mutex_wait_sensor);
  return r;
}
