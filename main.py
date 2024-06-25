import picozero
import time

# This main program just lets me know that code is running on the board by
# lighting up the built-in LED for a couple of seconds.
#
# See `./i2c-noodle-russian.py` for what I've really been up to. 

print('Here I am.')
picozero.pico_led.on()
time.sleep(2)
picozero.pico_led.off()
print('Here I go.')
