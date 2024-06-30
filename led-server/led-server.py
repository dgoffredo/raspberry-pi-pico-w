import asyncio
import machine
import network
import picozero
import rp2
import secrets
import socket
import time


gpio_led = picozero.LED(2)
onboard_led = machine.Pin("LED", machine.Pin.OUT)


def ignore(*args, **kwargs):
  pass


def wifi_connect(SSID, password, log=ignore):
  rp2.country('US') # 🇺🇸 🦅
  wlan = network.WLAN(network.STA_IF)
  wlan.active(True)
  # Disable power saver mode. (`pm` means "power mode")
  wlan.config(pm=0xa11140)
  wlan.connect(SSID, password)
  
  log('Connecting to wireless network', end='')
  while not wlan.isconnected():
    log('.', end='')
    time.sleep(0.5)
  log('') # for the newline
  
  IP, subnet, gateway, DNS = wlan.ifconfig()
  return IP

html_encoded = """<!DOCTYPE html>
  <html>
    <head><title>Pico W</title></head>
    <body><h1>Pico W</h1>
      <p>We got 'em.</p>
    </body>
  </html>
""".encode('utf8')

response_headers_and_body = b"""Content-Type: text/html; charset=utf-8\r
Content-Length: {}\r
\r
""".format(len(html_encoded)) + html_encoded

async def serve_client(reader, writer):
  print("Client connected")
  request_line = await reader.readline()
  print("Request line:", request_line)
  # Chew through the request headers, if any.
  while True:
    header = await reader.readline()
    if header == b'\r\n':
      break
    print('header...', header)

  if request_line.find(b'/light/on') == 4:
    gpio_led.on()
    status = 200
  elif request_line.find(b'/light/off') == 4:
    gpio_led.off()
    status = 200
  else:
    status = 404

  writer.write(b'HTTP/1.1 {} \r\n'.format(status))
  writer.write(response_headers_and_body)

  await writer.drain()
  await writer.wait_closed()
  print("Client disconnected")


async def main():
  IP = wifi_connect(SSID='Annoying Saxophone', password=secrets.wifi_password, log=print)
  print(f'Joined network as {IP}')
  onboard_led.on()
  await asyncio.start_server(serve_client, "0.0.0.0", 80)
    

event_loop = asyncio.get_event_loop()
event_loop.run_until_complete(main())
event_loop.run_forever()
