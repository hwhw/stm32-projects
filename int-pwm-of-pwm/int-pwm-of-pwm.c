#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/nvic.h>

int _write(int file, char *ptr, int len);

static void clock_setup(void)
{
	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	/* Enable clocks for GPIO port B, C */
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);

	/* Enable TIM3+4 Periph */
	rcc_periph_clock_enable(RCC_TIM3);
	rcc_periph_clock_enable(RCC_TIM4);
}

static void nvic_setup(void)
{
	/* Without this the timer interrupt routine will never be called. */
	nvic_enable_irq(NVIC_TIM3_IRQ);
	nvic_set_priority(NVIC_TIM3_IRQ, 1);
}


/* 10 us per timer tick: */
#define SIGNAL_PRESCALER 720

static void pwm_setup(void) {
	nvic_setup();

	timer_reset(TIM3);

	/* set up TIM3 for PWM of the signal */
	timer_set_mode(TIM3, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_set_prescaler(TIM3, SIGNAL_PRESCALER);
	timer_update_on_overflow(TIM3); // update interrupt only on overflow
	timer_enable_irq(TIM3, TIM_DIER_UIE); // enable interrupt on update (only overflow here)
	//timer_enable_preload(TIM3);
	//timer_set_period(TIM3, SIGNAL_PHASE/2);
	timer_continuous_mode(TIM3);
	// timer_one_shot_mode(TIM3);
}

static void carrier_setup(uint16_t carrier_phase) {
	timer_reset(TIM4);

	/* set up TIM4 as a slave of TIM3 for PWM of the carrier */
	timer_set_mode(TIM4, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_set_prescaler(TIM4, 0);

	/* set up TIM4 OC, output on GPIO */
	timer_disable_oc_output(TIM4, TIM_OC1);
	timer_set_oc_mode(TIM4, TIM_OC1, TIM_OCM_PWM1);
	timer_set_oc_value(TIM4, TIM_OC1, carrier_phase >> 1);
	timer_set_oc_polarity_high(TIM4, TIM_OC1);

	timer_continuous_mode(TIM4);
	timer_set_period(TIM4, carrier_phase);
	
	/* enable TIM4, will be gated by TIM3 */
	timer_enable_counter(TIM4);
}

#if 0
static void timer_high(void) {
	TIM_CCER(TIM4) |= TIM_CCER_CC1E; /* enable OC1 output for TIM4 (carrier) */
}
#endif

static void timer_low(void) {
	TIM_CCER(TIM4) &= ~TIM_CCER_CC1E; /* disable OC1 output for TIM4 (carrier) */
}

static void timer_toggle(void) {
	TIM_CCER(TIM4) ^= TIM_CCER_CC1E; /* toggle OC1 output for TIM4 (carrier) */
}

static void direct_low(void) {
	gpio_clear(GPIOB, GPIO6);
}

static void direct_toggle(void) {
	gpio_toggle(GPIOB, GPIO6);
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

#include "WORLDcodes.cpp"

#define C_STOP -1
#define C_END -2

/* 205ms spacing between codes */
#define SPACE 20500

static volatile bool running = false;
void tim3_isr(void)
{
	const struct IrCode **end_code = ir_codes + (sizeof(ir_codes) / sizeof(*ir_codes));
	static uint16_t cur_pair = 0;
	static uint8_t cur_pair_elem = 0; // 0 or 1
	static struct IrCode **cur_code = (struct IrCode **)ir_codes;
	static void (*handle_toggle)(void) = &timer_toggle;
	static void (*handle_low)(void) = &timer_low;

isr_restart:
	if(cur_code == (struct IrCode **)end_code) {
		// we've finished the list of all codes.
		// reset state and stop.
		cur_code = (struct IrCode **)ir_codes;
		cur_pair = 0;
		(*handle_low)();
		timer_disable_counter(TIM3);
		running = false;
		goto isr_done;
	}

	if(cur_pair >= (*cur_code)->numpairs) {
		// we've sent all the on/off pairs for the
		// current code. prepare to handle the next
		// one:
		cur_code++;
		cur_pair = 0;
		// but don't start right away, rather, pause
		// for a given amount of inter-code spacing time
		timer_set_period(TIM3, SPACE);
		goto isr_done;
	}

	if((cur_pair == 0) && (cur_pair_elem == 0)) {
		// we're at the start of a new code. set up
		(*handle_low)();
		// the carrier - if any.
		if((*cur_code)->timer_val == 0) {
			timer_disable_counter(TIM4);
			/* output will be on pin GPIO_TIM4_CH1 = PB6 */
			gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
			    GPIO_CNF_OUTPUT_PUSHPULL, GPIO6 );
			// no carrier, just on/off coding
			handle_toggle = &direct_toggle;
			handle_low = &direct_low;
		} else {
			/* output will be on pin GPIO_TIM4_CH1 = PB6 */
			gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
			    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM4_CH1 );
			// set up TIM4 for carrier generation
			carrier_setup((*cur_code)->timer_val);
			handle_toggle = &timer_toggle;
			handle_low = &timer_low;
		}
	}

	// each on/off pair is identified by <bitcompression> bits of
	// data in the "codes" array. most significant bit(s) in a byte
	// is/are used first.
	// overall, numpairs of on/off pairs are encoded.
	uint8_t byte = (cur_pair * (*cur_code)->bitcompression) >> 3;
	uint8_t bit = (cur_pair * (*cur_code)->bitcompression) % 8;

	uint8_t t_idx = (*cur_code)->codes[byte];
	t_idx = t_idx >> (8-(bit + (*cur_code)->bitcompression));
	t_idx = t_idx & (0xFF >> (8-(*cur_code)->bitcompression));

	// t_idx now contains the dictionary id of the pair to be sent
	// now look up the pair timings in the dictionary.

	int16_t	timing = (*cur_code)->times[(t_idx<<1) + cur_pair_elem];

	// two values for each pair, one "on" timing, one "off" timing
	cur_pair_elem ^= 1;
	if(cur_pair_elem == 0) {
		cur_pair++;
	}

	// toggle carrier/signal output */
	(*handle_toggle)();

	if(timing == 0)
		goto isr_restart;
	timer_set_period(TIM3, timing);

isr_done:
	TIM_SR(TIM3) &= ~TIM_SR_UIF; /* Clear interrrupt flag. */
}

int main(void)
{
	clock_setup();
	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);
	gpio_clear(GPIOC, GPIO13);
	pwm_setup();

	while (1) {
		running = true;
		timer_set_period(TIM3, 1); // short one
		timer_enable_counter(TIM3);

		/* wait until done */
		while(running) { __asm__("wfi"); }
		gpio_toggle(GPIOC, GPIO13);
	}

	return 0;
}
