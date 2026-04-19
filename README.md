This is a IoT project that makes use of the ESP RainMaker app to randomly toggle colours 
of 3 RGB LEDs, as if rolling on a slot machine. It is a fun project for me to learn ESP 
IDF and RainMaker.

Initially, upon connecting the ESP to power, the LEDs will be in the off state. By turning 
it on in the app, the LEDs will randomly cycle through possible colours, before stopping 
one by one on the final (random) colour. If all three colours are the same, it is a jackpot.

There are a total of 6 possible colours: red, yellow, green, cyan, magenta, and white.
