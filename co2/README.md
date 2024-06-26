## `co2.c`
I2C blocks forever on write. Haven't figured out why yet.
```
3: Hello! I'm going to sleep for two seconds, ten times.
4: Hello! I'm going to sleep for two seconds, ten times.
5: Hello! I'm going to sleep for two seconds, ten times.
6: Hello! I'm going to sleep for two seconds, ten times.
7: Hello! I'm going to sleep for two seconds, ten times.
8: Hello! I'm going to sleep for two seconds, ten times.
9: Hello! I'm going to sleep for two seconds, ten times.
10: Hello! I'm going to sleep for two seconds, ten times.
The actual I2C baudrate is 399361 Hz
[/home/david/src/raspberry-pi-pico-w/co2/co2.c:41]
[/home/david/src/raspberry-pi-pico-w/co2/co2.c:43]
[/home/david/src/raspberry-pi-pico-w/co2/co2.c:45]
[/home/david/src/raspberry-pi-pico-w/co2/co2.c:47]
[/home/david/src/raspberry-pi-pico-w/co2/co2.c:49]
[/home/david/src/raspberry-pi-pico-w/embedded-i2c-scd4x/sample-implementations/RaspberryPi_Pico/sensirion_i2c_hal.c:121]
going to write 2 bytes to address 0x62: 0x36 0x82
```

## `sweep.c`
Now I have an idea why I2C blocks forever on write: there's nothing on the bus.

[sweep.c](sweep.c) is based on the [SDK example][1], and when I run it on my
setup, the table reports no slaves.

This is strange, because calling the equivalent functionality from micropython
yields `0x62`, the SCD41.

I haven't figured out what the difference is yet.

[1]: https://github.com/raspberrypi/pico-examples/blob/master/i2c/bus_scan/bus_scan.c
