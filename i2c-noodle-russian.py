# This code began as `./SCD4x/main.py`, but I've since taken over.

import machine
import picozero
import scd4x_sensirion
from sensor_pack.bus_service import I2cAdapter
import sys
import time

i2c = machine.I2C(
    id=0, # Which IC2 bus, e.g. 0 for I2C0, 1 for I2C1
    sda=machine.Pin(12),
    scl=machine.Pin(13),
    freq=400_000)
 
print('Scanning i2c bus...')
devices = i2c.scan()
print('Devices:', [hex(device_id) for device_id in devices])

adaptor = I2cAdapter(i2c)
# sensor
sen = scd4x_sensirion.SCD4xSensirion(adaptor)

# This section all works. You can loop it as many times as you like.
for _ in range(1):
    sid = sen.get_id()
    print(f"Sensor id 3 x Word: {sid[0]:x}:{sid[1]:x}:{sid[2]:x}")
    # t_offs = 0.0
    # Warning: To change or read sensor settings, the SCD4x must be in idle mode!!!
    # Otherwise an EIO exception will be raised!
    # print(f"Set temperature offset sensor to {t_offs} Celsius")
    # sen.set_temperature_offset(t_offs)
    t_offs = sen.get_temperature_offset()
    print(f"Get temperature offset from sensor: {t_offs} Celsius")
    # masl = 160  # Meter Above Sea Level
    # print(f"Set my place M.A.S.L. to {masl} meter")
    # sen.set_altitude(masl)
    masl = sen.get_altitude()
    print(f"Get M.A.S.L. from sensor: {masl} meter")
    # data ready
    if sen.is_data_ready():
        print("Measurement data can be read")  # Данные измерений могут быть прочитаны!
    else:
        print("Measurement data is not ready")

    if sen.is_auto_calibration():
        print("The automatic self-calibration is ON")
    else:
        print("The automatic self-calibration is OFF")

# TODO: Reading the response after sending a self-test command fails with EIO on read.
if False:
    # Run an end-of-line diagnostic on the sensor.
    print('Performing sensor self-test')
    if sen.exec_self_test():
        print('Sensor self-test did not find any issues.')
    else:
        print('Sensor self-test found an issue.')

# TODO: Any command sent after setting the sensor to single-shot mode fails with EIO on write.
if False:
    # Single shot measurement
    # Note that the `start` parameter is ignored when `single_shot == True`.
    print('Sending single-shot command')
    sen.set_measurement(start=True, single_shot=True)

    seconds = 6
    print(f'sleeping for {seconds} seconds')
    time.sleep(6)

    check = True
    if check:
        if sen.is_data_ready():
            print("Measurement data can be read")
        else:
            print("Measurement data is not ready")

    print('reading measurement data')
    co2, t, rh = sen.get_meas_data()
    print(f"CO2 [ppm]: {co2}; T [°C]: {t}; RH [%]: {rh}")

# TODO: Any command sent after setting the sensor to measurement mode fails with EIO on write.
if True:
    # 1. Go into periodic sensor mode,
    # 2. sleep for ~10 seconds,
    # 3. check if data is ready.
    # 4. If data is not ready, sleep for a second and goto 3.
    # 5. If data is ready, read the data.

    # 1:
    print('Setting sensor to periodic measurement mode.')
    sen.set_measurement(start=True)

    def check():
        print('Checking if measurement data is ready.')
        return sen.is_data_ready()

    while True:
        # 2:
        seconds = 5
        print(f'sleeping for {seconds} seconds')
        time.sleep(seconds)
        # 3:
        while not check():
           # 4:
            seconds = 1
            print('Data is not ready. Sleeping for {seconds} seconds')
            time.sleep(seconds)
        # 5:
        print('Reading measurement data')
        co2, t, rh = sen.get_meas_data()
        print(f"CO2 [ppm]: {co2}; T [°C]: {t}; RH [%]: {rh}")

# TODO: I disabled the rest of the module author's example code.
sys.exit()

repeat = 5
multiplier = 2
wt = sen.get_conversion_cycle_time()
print(f"conversion cycle time [ms]: {wt}")

sen.set_measurement(start=True, single_shot=False)      # periodic start
print("Periodic measurement started")
for i in range(repeat):
    time.sleep_ms(wt)
    if sen.is_data_ready():
        print("Measurement data can be read!")  # Данные измерений могут быть прочитаны!
        co2, t, rh = sen.get_meas_data()
        print(f"CO2 [ppm]: {co2}; T [°C]: {t}; RH [%]: {rh}")
    else:
        print("Measurement data missing!")

print(20*"*_")
print("Reading using an iterator!")
for counter, items in enumerate(sen):
    time.sleep_ms(wt)
    if items:
        co2, t, rh = items
        print(f"CO2 [ppm]: {co2}; T [°C]: {t}; RH [%]: {rh}")
        if repeat == counter:
            break

print(20 * "*_")
print("Using single shot mode!")
# Force return sensor in IDLE mode!
# Принудительно перевожу датчик в режим IDLE!
sen.set_measurement(start=False, single_shot=False)
cnt = 0
while cnt <= repeat:
    sen.set_measurement(start=False, single_shot=True, rht_only=False)
    time.sleep_ms(multiplier * wt)
    # TODO: Maybe we have to keep retrying? Or is it in a borked state?
    while True:
        try:
            print('about to call is_data_ready')
            if sen.is_data_ready():
                print('sensor is ready. About to call get_meas_data')
                co2, t, rh = sen.get_meas_data()
                print(f"CO2 [ppm]: {co2}; T [°C]: {t}; RH [%]: {rh}")
                break
            else:
                print('Data is not yet ready.')
        except Exception as err:
            print(err)
            time.sleep_ms(multiplier * wt)
    cnt += 1

# Принудительно перевожу датчик в режим IDLE!
# sen.set_measurement(start=False, single_shot=False)
sen.set_measurement(start=False, single_shot=True, rht_only=True)   # rht only mode!
wt = sen.get_conversion_cycle_time()
print(20 * "*_")
# Использование режима измерения по запросу! Только относительная влажность и температура измеряются датчиком!
# относительная влажность + температура. CO2 всегда равна нулю или не изменяется!!
print("Using single shot mode! RH + T only! (Temp + RH. CO2 always zero or does not change!!)")
while True:
    time.sleep_ms(multiplier * wt)
    co2, t, rh = sen.get_meas_data()
    print(f"CO2 [ppm]: {co2}; T [°C]: {t}; RH [%]: {rh}")
    sen.set_measurement(start=False, single_shot=True, rht_only=True)   # rht only mode!

