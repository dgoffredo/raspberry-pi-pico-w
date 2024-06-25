# This is my playing around with the WiFi chip and the built-in LED.

import machine
import network
import socket
import time
import picozero
import secrets

onboard_led = machine.Pin("LED", machine.Pin.OUT)
gpio_led = picozero.LED(13)
timer = machine.Timer()

def timer_callback(timer):
    onboard_led.toggle()
    gpio_led.toggle()

def wifi_connect(SSID, password):
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    wlan.connect(SSID, password)
    
    print('Connecting to wireless network', end='')
    while not wlan.isconnected():
        print('.', end='')
        time.sleep(1)
    print('') # for the newline
    
    IP, subnet, gateway, DNS = wlan.ifconfig()
    return IP

timer.init(
  freq=2.5,
  mode=machine.Timer.PERIODIC,
  callback=timer_callback)

try:
    IP = wifi_connect(SSID='Annoying Saxophone', password=secrets.wifi_password)
    print(f'Joined network as {IP}')
    # TODO: serve webpage
except KeyboardInterrupt:
    machine.reset()

