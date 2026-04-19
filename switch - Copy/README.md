# Switch Example

## Build and Flash firmware

Follow the ESP RainMaker Documentation [Get Started](https://rainmaker.espressif.com/docs/get-started.html) section to build and flash this firmware. Just note the path of this example.

## What to expect in this example?

- This example uses the BOOT button and three RGB LEDs to demonstrate a switch-driven slot machine.
- Turning the switch ON (from button or RainMaker app) starts a slot animation where all three LEDs cycle through preset colours, then stop one by one on random colours.
- Turning the switch OFF stops the effect and turns all LEDs off.
- Red and green channels for each LED use PWM (6 channels total). Blue channels are on/off GPIO only.
- Toggling the switch on the phone app also prints messages like these on the ESP32 monitor:

```
I (16073) app_main: Received value = true for Switch - power
```

### Slot colours

| Colour | R | G | B |
| --- | ---: | ---: | ---: |
| Red | 255 | 0 | 0 |
| Yellow | 255 | 180 | 0 |
| Green | 0 | 255 | 0 |
| Magenta | 255 | 0 | 255 |
| Cyan | 0 | 180 | 255 |
| White | 180 | 180 | 255 |

### LED pin configuration

Set the LED pins in `idf.py menuconfig` under **Example Configuration**:

- `RGB_LED_1_*_GPIO`
- `RGB_LED_2_*_GPIO`
- `RGB_LED_3_*_GPIO`

Each LED uses PWM for red/green and GPIO on/off for blue.

### Reset to Factory

Press and hold the BOOT button for more than 3 seconds to reset the board to factory defaults. You will have to provision the board again to use it.
