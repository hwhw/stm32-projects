#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>

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

/* 1 us per timer tick: */
#define SIGNAL_PRESCALER 72
#define SIGNAL_PHASE 1120

/* 0.5 us per timer tick: */
#define CARRIER_PRESCALER 144
/* 25 us per phase (40kHz) */
#define CARRIER_PHASE 50
#define CARRIER_DUTY_CYCLE (CARRIER_PHASE/2)

static void pwm_setup(void) {
	/* output will be on pin GPIO_TIM4_CH1 = PB6 */
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
	    GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM4_CH1 );

	timer_reset(TIM3);
	timer_reset(TIM4);

	/* set up TIM3 for PWM of the signal */
	timer_set_mode(TIM3, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_set_master_mode(TIM3, TIM_CR2_MMS_COMPARE_OC1REF); // master mode, output OC1 signal
	timer_set_prescaler(TIM3, SIGNAL_PRESCALER);

	/* set up TIM3 OC, no direct output on GPIO */
	timer_set_oc_mode(TIM3, TIM_OC1, TIM_OCM_PWM1);
	timer_set_oc_value(TIM3, TIM_OC1, 0); // will be fed outside of this function
	timer_enable_oc_preload(TIM3, TIM_OC1);
	timer_set_oc_polarity_high(TIM3, TIM_OC1); // is this needed?

	timer_enable_preload(TIM3);
	timer_continuous_mode(TIM3);
	timer_set_period(TIM3, SIGNAL_PHASE);

	/* set up TIM4 as a slave of TIM3 for PWM of the carrier */
	timer_set_mode(TIM4, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_slave_set_mode(TIM4, TIM_SMCR_SMS_GM); // slave in gated mode
	timer_slave_set_trigger(TIM4, TIM_SMCR_TS_ITR2); // master timer TIM3 will be on ITR2 for TIM4 (see ref. manual crossref table) 

	timer_set_prescaler(TIM4, CARRIER_PRESCALER);

	/* set up TIM4 OC, output on GPIO */
	timer_disable_oc_output(TIM4, TIM_OC1);
	timer_set_oc_mode(TIM4, TIM_OC1, TIM_OCM_PWM1);
	timer_set_oc_value(TIM4, TIM_OC1, CARRIER_DUTY_CYCLE);
	timer_set_oc_polarity_high(TIM4, TIM_OC1);
	timer_enable_oc_output(TIM4, TIM_OC1);

	//timer_enable_preload(TIM4);
	timer_continuous_mode(TIM4);
	timer_set_period(TIM4, CARRIER_PHASE);
	
	/* enable TIM4, will be gated by TIM3 */
	timer_enable_counter(TIM4);

	/* enable TIM3 */
	timer_enable_counter(TIM3);
}

#define D1 (SIGNAL_PHASE/2)
#define FULL SIGNAL_PHASE
static const uint16_t values[] = {
	/* preamble: */
	FULL, FULL, FULL, FULL, FULL, FULL, FULL, FULL, 0, 0, 0, 0,

	/* data: */
	D1,  0, D1, D1,  0,  0, D1,  0,
	/* ... */

	0
};

#define DELAY 8000000

int main(void)
{
	unsigned int i;

	clock_setup();
	pwm_setup();

	while (1) {
		for(i=0; i<(sizeof(values)/sizeof(*values)); i++) {
			timer_set_oc_value(TIM3, TIM_OC1, values[i]);
			// reset update flag
			TIM3_SR &= ~TIM_SR_UIF;
			// wait for update flag getting set by timer
			while(!(TIM3_SR & TIM_SR_UIF)) { /* wait... */ }
		}
		for(i=0; i<DELAY; i++) __asm__("nop");
	}

	return 0;
}
