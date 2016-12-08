#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "usbbb.h"

void output_sensors(uint16_t data[]) {
  printf("\e[1;1H\e[2J"); /* clear screen */
  for(int r=0; r<8; r++) {
    printf("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
        data[10*r],
        data[10*r+1],
        data[10*r+2],
        data[10*r+3],
        data[10*r+4],
        data[10*r+5],
        data[10*r+6],
        data[10*r+7],
        data[10*r+8],
        data[10*r+9],
        data[10*r+10],
        data[10*r+11]);
  }
}

void get_led_pos(int led, int pos[]) {
  int x, y;
  if(led < 160) {
    // left-right
    y = 5 + 4 * (led / 20);
    x = 2 * (led % 20);
    if(((led / 20) % 2) == 1)
      x = 38 - x;
  } else {
    // down-up
    led -= 160;
    x = 5 + 4 * (led / 20);
    y = 2 * (led % 20);
    if(((led / 20) % 2) == 0)
      y = 38 - y;
  }
  pos[0] = x;
  pos[1] = y;
}

int16_t pos_led[40][40];
int16_t pos_leds10[10][10][4];
void init_pos_led() {
  memset(pos_led, 0xFF, 40*40*sizeof(int16_t));
  memset(pos_leds10, 0xFF, 10*10*4*sizeof(int16_t));
  for(int i=0; i<320; i++) {
    int p[2];
    get_led_pos(i, p);
    pos_led[p[1]][p[0]] = i;
    int16_t *p10 = pos_leds10[p[1]/4][p[0]/4];
    while(*p10 != -1) p10++;
    *p10 = i;
  }
#ifdef DEBUG
  for(int y=0; y<40; y++) {
    for(int x=0; x<40; x++) {
      printf("%3d, ", pos_led[y][x]);
    }
    printf("\n");
  }
  for(int y=0; y<10; y++) {
    for(int i=0; i<4; i++) {
    for(int x=0; x<10; x++) {
      printf("%3d, ", pos_leds10[y][x][i]);
    }
    printf("\n");
    }
  }
#endif
}

void set_led10(bb_ctx* bb, const int x, const int y, const int r, const int g, const int b) {
  for(int i=0; i<4; i++) {
    int led = pos_leds10[y][x][i];
    if(led == -1) break;
    bb_set_led(bb, led, r, g, b);
  }
}

void set_led40(bb_ctx* bb, const int x, const int y, const int r, const int g, const int b) {
  int led = pos_led[y][x];
  if(led != -1)
    bb_set_led(bb, led, r, g, b);
}

void HSL_to_RGB(int hue, int sat, int lum, int* r, int* g, int* b)
{
    int v;

    v = (lum < 128) ? (lum * (256 + sat)) >> 8 :
          (((lum + sat) << 8) - lum * sat) >> 8;
    if (v <= 0) {
        *r = *g = *b = 0;
    } else {
        int m;
        int sextant;
        int fract, vsf, mid1, mid2;

        m = lum + lum - v;
        hue *= 6;
        sextant = hue >> 8;
        fract = hue - (sextant << 8);
        vsf = v * fract * (v - m) / v >> 8;
        mid1 = m + vsf;
        mid2 = v - vsf;
        switch (sextant) {
           case 0: *r = v; *g = mid1; *b = m; break;
           case 1: *r = mid2; *g = v; *b = m; break;
           case 2: *r = m; *g = v; *b = mid1; break;
           case 3: *r = m; *g = mid2; *b = v; break;
           case 4: *r = mid1; *g = m; *b = v; break;
           case 5: *r = v; *g = m; *b = mid2; break;
        }
    }
}
void paint10(bb_ctx* bb) {
  static int h = 0;
  static int s = 255;
  static int l = 128;
  for(int y=0; y<10; y++) {
    for(int x=0; x<10; x++) {
      int r, g, b;
      HSL_to_RGB((h+x*4+(y/2)) % 256, s, l, &r, &g, &b);
      set_led10(bb, x, y, r, g, b);
    }
  }
  bb_transmit(bb);
  h++;
}
void paint40(bb_ctx* bb) {
  static float h = 0;
  static int s = 255;
  static int l = 128;
  //static float r = 0;
  for(int y=0; y<40; y++) {
    for(int x=0; x<40; x++) {
      int r, g, b;

      //float hoffs=cosf(r)*(x-20) - sinf(r)*(y-20);
      //hoffs+=100;
      float hoffs = x*3 + y/2;

      HSL_to_RGB((int)floor(h+hoffs) % 256, s, l, &r, &g, &b);
      set_led40(bb, x, y, r, g, b);
    }
  }
  bb_transmit(bb);
  h+=.5;
  //r+=.1;
}

int main(int argc, char* argv[]) {
  bb_ctx* bb;
  init_pos_led();
  //exit(0);
  int err = bb_open(&bb);
  if(err) { 
    fprintf(stderr, "Error opening device: %d\n", err);
    exit(-1);
  }
  while(1) {
    paint40(bb);
    //output_sensors(bb->sensors);
    usleep(20000);
  }
}
