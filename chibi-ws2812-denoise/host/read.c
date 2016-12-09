// nanosleep(), please:
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <math.h>
//#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "usbbb.h"

void output_sensors(bb_ctx *bb) {
  uint16_t data[8*12];
  bb_get_sensordata(bb, data);
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
      bb_set_led10(bb, x, y, r, g, b);
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
      bb_set_led40(bb, x, y, r, g, b);
    }
  }
  bb_transmit(bb);
  h+=.5;
  //r+=.1;
}

int main(int argc, char* argv[]) {
  struct timespec tick = { 0, 20*1000*1000 };
  bb_ctx* bb;
  //exit(0);
  int err = bb_open(&bb);
  if(err) { 
    fprintf(stderr, "Error opening device: %d\n", err);
    exit(-1);
  }
  while(1) {
    paint40(bb);
    //output_sensors(bb);
    nanosleep(&tick, NULL);
  }
}
