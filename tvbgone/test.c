#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

static int out = 0;
static void out_high() {
	out = 1;
	printf("out: %d\n", out);
}

static void out_low() {
	out = 0;
	printf("out: %d\n", out);
}

static void out_toggle() {
	out = 1-out;
	printf("out: %d\n", out);
}

struct IrCode {
	uint16_t timer_val;
	uint8_t numpairs;
	uint8_t bitcompression;
	uint16_t const *times;
	uint8_t const*codes;
};


#define freq_to_timerval(x) (72000000/x)
#define PROGMEM
const uint16_t code_na000Times[] PROGMEM = {
  60, 60,
  60, 2700,
  120, 60,
  240, 60,
};
const uint8_t code_na000Codes[] PROGMEM = {
  0xE2,
  0x20,
  0x80,
  0x78,
  0x88,
  0x20,
  0x10,
};
const struct IrCode code_na000Code PROGMEM = {
  freq_to_timerval(38400),
  26,             // # of pairs
  2,              // # of bits per index
  code_na000Times,
  code_na000Codes
};
const struct IrCode *ir_codes[] = {
	&code_na000Code,
};

#define C_STOP -1
#define C_END -2

static int16_t get_timing() {
	static const struct IrCode **end_code = ir_codes + (sizeof(ir_codes) / sizeof(*ir_codes));
	static uint16_t cur_pair = 0;
	static uint8_t cur_pair_elem = 0; // 0 or 1
	static struct IrCode **cur_code = ir_codes;

	if(cur_code == end_code) {
		cur_code = ir_codes;
		cur_pair = 0;
		return C_END;
	}

	if(cur_pair >= (*cur_code)->numpairs) {
		cur_code++;
		cur_pair = 0;
		return C_STOP;
	}

	if((cur_pair == 0) && (cur_pair_elem == 0)) {
		printf("carrier_setup: %d\n", ((*cur_code)->timer_val));
	}

	uint8_t byte = (cur_pair * (*cur_code)->bitcompression) >> 3;
	uint8_t bit = (cur_pair * (*cur_code)->bitcompression) % 8;

	uint8_t t_idx = (*cur_code)->codes[byte];
	printf("  cur_pair is %d (=byte %d, bit %d, bc %d) -> ", cur_pair, byte, bit, (*cur_code)->bitcompression);
	t_idx = t_idx >> (8-(bit + (*cur_code)->bitcompression));
	t_idx = t_idx & (0xFF >> (8-(*cur_code)->bitcompression));

	int16_t timing = (*cur_code)->times[(t_idx<<1) + cur_pair_elem];
	printf("%d+%d -> %d\n", t_idx, cur_pair_elem, timing);
	cur_pair_elem ^= 1;
	if(cur_pair_elem == 0) {
		cur_pair++;
	}

	return timing;
}

/* 205ms spacing between codes */
#define SPACE 20500

static volatile bool running = false;
void tim3_isr(void)
{
	/*
		at this point, the last value written to the period length got latched.
		prepare and write the next one to the preload register
	*/
	int16_t next = get_timing();
	if(next == C_STOP) {
		next = SPACE;
	} else if(next == C_END) {
		out_low();
		printf("STOP.\n");
		//timer_disable_counter(TIM3);
		running = false;
		return;
	} else {
		out_toggle();
	}
	printf("write to timer preload: %d\n", next);
}

int main(void)
{
	while (1) {
		tim3_isr();
	}

	return 0;
}
