import asyncio
import json
import machine
import network
import rp2
import secrets
import socket
import sys
import time

# SCD41 sensor stuff
import scd4x_sensirion
from sensor_pack.bus_service import I2cAdapter


onboard_led = machine.Pin("LED", machine.Pin.OUT)


def log(*args, **kwargs):
  return print(*args, **kwargs)


async def wifi_connect(SSID, password) -> str:
  rp2.country('US') # 🇺🇸 🦅
  wlan = network.WLAN(network.STA_IF)
  wlan.active(True)
  # Disable power saver mode. (`pm` means "power mode")
  # wlan.config(pm=0xa11140)
  wlan.connect(SSID, password)
  
  log('Connecting to wireless network', end='')
  # If we were previously connected to the network and then powered down,
  # the next attempt at connecting will stall.
  # So, if it takes more than a few seconds to connect, shut down the wifi
  # chip and try again in a couple of seconds.
  i = 0
  while not wlan.isconnected():
    if i == 12:
      log('!', end='')
      wlan.active(False)
      await asyncio.sleep(3)
      wlan.active(True)
      wlan.connect(SSID, password)
    else:
      log('.', end='')
      await asyncio.sleep(0.5)
    i += 1
  log('')
  
  IP, subnet, gateway, DNS = wlan.ifconfig()
  return IP


def response_with_body(body: bytes) -> bytes:
  return b"""HTTP/1.1 200 OK\r
Connection: close\r
Content-Type: application/json; charset=utf-8\r
Content-Length: {}\r
\r
""".format(len(body)) + body


not_found_response = b"""HTTP/1.1 404 GTFO\r
Connection: close\r
Content-Length: 8\r
Content-Type: text/plain\r
\r
Go away!"""

# Latest measurement data.
# Fields:
# - sequence_number (int)
# - CO2_ppm (int)
# - temperature_celsius (float)
# - relative_humidity_percent (float)
latest = {}


async def monitor_sensor():
  global latest

  i2c = machine.I2C(
      id=0, # Which IC2 bus, e.g. 0 for I2C0, 1 for I2C1
      sda=machine.Pin(12), # GPIO pin
      scl=machine.Pin(13), # GPIO pin
      freq=400_000,
      timeout=50_000)

  log('Waiting for SCD41 to appear on I2C bus.', end='')
  while True:
    devices = i2c.scan()
    if 0x62 in devices:
      break
    await asyncio.sleep(1)
    log('.', end='')
  log('\nI2C devices:', [hex(device_id) for device_id in devices])

  adaptor = I2cAdapter(i2c)
  sensor = scd4x_sensirion.SCD4xSensirion(adaptor)

  log('Stopping sensor measurements, in case they were previously started.')  
  sensor.set_measurement(start=False)

  log('Disabling sensor automatic self-calibration (ASC).')
  sensor.set_auto_calibration(False)

  log('Setting sensor to periodic measurement mode.')
  sensor.set_measurement(start=True)

  while True:
      try:
          await asyncio.sleep(2.5)
          prev_seq = latest.get('sequence_number', -1)
          log(f'{prev_seq}.', end='')
          while not sensor.is_data_ready():
            await asyncio.sleep(2.5)
            log('.', end='')

          co2, t, rh = sensor.get_meas_data()
          latest = {
            'sequence_number': prev_seq + 1,
            'CO2_ppm': co2,
            'temperature_celsius': t,
            'relative_humidity_percent': rh
          }
      except Exception as err:
          sys.print_exception(err)


async def serve_client(reader, writer):
  log("Client connected")
  request_line = await reader.readline()
  log("Request line:", request_line)
  # Chew through the request headers, if any.
  while True:
    header = await reader.readline()
    if header == b'\r\n':
      break
    log('header...', header)

  if request_line.find(b'/sensor/latest.json') == 4:
    body = json.dumps(latest).encode('utf8')
    writer.write(response_with_body(body))
  else:
    writer.write(not_found_response)


  await writer.drain()
  await writer.wait_closed()
  log("Client disconnected")


async def main():
  onboard_led.on()
  IP = await wifi_connect(SSID='Annoying Saxophone', password=secrets.wifi_password)
  log(f'Joined network as {IP}')
  onboard_led.off()
  await asyncio.start_server(serve_client, "0.0.0.0", 80)
  await monitor_sensor()
    

event_loop = asyncio.get_event_loop()
event_loop.run_until_complete(main())
event_loop.run_forever()
