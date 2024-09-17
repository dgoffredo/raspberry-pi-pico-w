from machine import Pin, I2C

class backpack:
    ADDRESS             = 0x70
    BLINK_CMD           = 0x80
    CMD_BRIGHTNESS      = 0xE0
    # Digits 0 - F
    NUMS = [0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D,
        0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71]

    def __init__(self, id, sda, scl):
        self.buffer = bytearray([0]*16)
        self.i2c=I2C(id=id,sda=Pin(sda), scl=Pin(scl))
        self.i2c.writeto(self.ADDRESS,b'\x21')
        # 0 to 3
        self.blink_rate(0)
        # 0 to 15
        self.set_brightness(15)
        self.update_display()

    def set_brightness(self,b):
        self.i2c.writeto(self.ADDRESS,bytes([self.CMD_BRIGHTNESS | b]))

    def blink_rate(self, b):
        self.i2c.writeto(self.ADDRESS,bytes([self.BLINK_CMD | 1 | (b << 1)]))

    def write_digit(self, position, digit, dot=False):
        # skip the colon
        offset = 0 if position < 2 else 1
        pos = offset + position
        self.buffer[pos*2] = self.NUMS[digit] & 0xFF
        if dot:
            self.buffer[pos*2] |= 0x80

    def update_display(self):
        data = bytearray([0]) + self.buffer
        self.i2c.writeto(self.ADDRESS,data)

    def print(self,value,leading_zeros=True):
        if value<0 or value>9999:
            return
        sdig =  '{:04d}'.format(value)
        dts = [int(x) for x in sdig]
        for i,d in enumerate(dts):
            if leading_zeros or d != 0:
              self.write_digit(i,d)

    def set_decimal(self, position, dot=True):
        # skip the colon
        offset = 0 if position < 2 else 1
        pos = offset + position
        if dot:
            self.buffer[pos*2] |= 0x80
        else:
            self.buffer[pos*2] &= 0x7F

    def clear(self):
        self.buffer = bytearray([0]*16)
        self.update_display()

    def set_colon(self, colon=True):
        if colon:
            self.buffer[4] |= 0x02
        else:
            self.buffer[4] &= 0xFD

def count(f):
    from time import sleep
    for i in range(10000):
        f.print(i, leading_zeros=False)
        f.update_display()
        sleep(0.05)

if __name__ == '__main__':
    # from ht16k33 import backpack
    from time import sleep

    # declare an instance
    f = backpack(id=1, sda=26, scl=27)
    # decimals on
    for i in range(4):
        f.set_decimal(i)
        f.update_display()
        sleep(0.1)
    # decimals off
    for i in range(4):
        f.set_decimal(i, False)
        f.update_display()
        sleep(0.1)

    # print something
    f.print(1234)
    f.update_display()
    sleep(0.1)
    # clear the display
    f.clear()
    sleep(0.1)
    # blink the colon
    for i in range(4):
        f.set_colon()
        f.update_display()
        sleep(0.5)
        f.set_colon(False)
        f.update_display()
        sleep(0.5)
    # do some counting
    for i in range(10000):
        f.print(i)
        f.update_display()
        sleep(0.05)
