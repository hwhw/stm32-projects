This is a stm32 implementation of the TVbGone software, originally
targeted at the ATTiny platform. Compared to that, we have some
luxury: CPU cycles en masse, Flash memory en masse, RAM en masse.
And we have some nice configurable timers.

This uses TIM4 to generate a carrier (about 40kHz) via PWM, TIM3
to generate interrupts for carrier on/carrier off and uses the
corresponding interrupt routine to switch on/off the PWM output.
