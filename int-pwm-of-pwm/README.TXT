This is a second iteration of the "modulated carrier" approach.
While the first one was working in principle, it came with a
problem: Since the first timer gated the second one, it stopped
the second timer in some cases when it's OC pin was in high state.
However, we always want it to be in low state during pauses.

So this approach goes a slightly different way: the first timer
controls an interrupt routine which starts/stops the second timer
- but always after finishing its current PWM cycle.
