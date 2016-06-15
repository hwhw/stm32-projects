#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/nvic.h>

int _write(int file, char *ptr, int len);

static void clock_setup(void)
{
	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	/* Enable clocks for GPIO port B */
	rcc_periph_clock_enable(RCC_GPIOB);

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


/* 1 us per timer tick: */
#define SIGNAL_PRESCALER 72
#define SIGNAL_PHASE 1120

/* 0.5 us per timer tick: */
#define CARRIER_PRESCALER 36
/* 25 us per phase (40kHz) */
#define CARRIER_PHASE 50
#define CARRIER_DUTY_CYCLE (CARRIER_PHASE/2)

static void pwm_setup(void) {
	nvic_setup();

	/* output will be on pin GPIO_TIM4_CH1 = PB6 */
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM4_CH1 );

	timer_reset(TIM3);
	timer_reset(TIM4);

	/* set up TIM3 for PWM of the signal */
	timer_set_mode(TIM3, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_set_prescaler(TIM3, SIGNAL_PRESCALER);
	timer_update_on_overflow(TIM3); // update interrupt only on overflow
	timer_enable_irq(TIM3, TIM_DIER_UIE); // enable interrupt on update (only overflow here)
	timer_set_period(TIM3, SIGNAL_PHASE/2);
	timer_continuous_mode(TIM3);

	/* set up TIM4 as a slave of TIM3 for PWM of the carrier */
	timer_set_mode(TIM4, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_set_prescaler(TIM4, CARRIER_PRESCALER);

	/* set up TIM4 OC, output on GPIO */
	timer_disable_oc_output(TIM4, TIM_OC1);
	timer_set_oc_mode(TIM4, TIM_OC1, TIM_OCM_PWM1);
	timer_set_oc_value(TIM4, TIM_OC1, CARRIER_DUTY_CYCLE);
	timer_set_oc_polarity_high(TIM4, TIM_OC1);

	timer_continuous_mode(TIM4);
	timer_set_period(TIM4, CARRIER_PHASE);
	
	/* enable TIM4, will be gated by TIM3 */
	timer_enable_counter(TIM4);
}

#define D0   0, 0
#define D1   1, 0
#define FULL 1, 1
static const uint8_t values[] = {
	/* preamble: */
	FULL, FULL, FULL, FULL, FULL, FULL, FULL, FULL, D0, D0, D0, D0,

	/* data: */
	D1, D0, D1, D1, D0, D0, D1, D0,
	/* ... */

	0
};
static volatile bool enabled = false;
static volatile uint16_t pos = 0;
static volatile uint16_t size = sizeof(values) / sizeof(*values);

void tim3_isr(void)
{
	if(enabled) {
		if(values[pos++]) {
			TIM_CCER(TIM4) |= TIM_CCER_CC1E; /* enable OC1 output for TIM4 (carrier) */
		} else {
			TIM_CCER(TIM4) &= ~TIM_CCER_CC1E; /* disable OC1 output for TIM4 (carrier) */
		}

		if(pos == size) {
			/* sequence done, disable TIM3 */
			enabled = false;
			pos = 0;
			timer_disable_counter(TIM3);
		}
	}

	TIM_SR(TIM3) &= ~TIM_SR_UIF; /* Clear interrrupt flag. */
}

#define DELAY 80000

int main(void)
{
	clock_setup();
	pwm_setup();

	while (1) {
		enabled = true;
		timer_enable_counter(TIM3);

		/* wait until done */
		while(enabled) { __asm__("wfi"); }

		//for(int i=0; i<DELAY; i++) __asm__("nop");
	}

	return 0;
}
