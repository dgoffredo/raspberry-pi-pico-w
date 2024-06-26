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
