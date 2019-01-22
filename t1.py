#!/usr/bin/env python
import time
import paho.mqtt.client as paho
import json
import requests
import os

user_button_gpio = 67
low_level_gpio = 68
high_level_gpio = 74
led_gpio = 75

user_button_pressed = 0
above_low_level = 0
above_high_level = 0

mqtt_broker="digi"
pump_ip=""
mqtt = paho.Client("client-001")
pump_running = False

def on_message(client, userdata, message):
    global pump_ip
    topic = message.topic.decode("utf-8")
    payload = message.payload.decode("utf-8")

    print("mqtt in : " + topic + ": " + payload)

    if topic == "node0/info":
        data = json.loads(payload)
        pump_ip = data["ip_addr"]
    elif topic == "node0/pump/activated":
        global pump_running
        pump_running = (payload == "1")

def mqtt_init():
    mqtt.on_message = on_message
    print("connecting")
    mqtt.connect(mqtt_broker)
    mqtt.loop_start()
    print("subscribing")
    mqtt.subscribe("node0/#")

def led(on=True):
    if not os.access('/sys/class/gpio/', os.W_OK):
        return
    with open('/sys/class/gpio/gpio'+str(led_gpio)+'/value', 'w') as file:
        file.write("%d" % on*1)

def pump_start(pump_on = True):
    if pump_ip == "":
        print "No pump in system"
        return
    led(pump_on)
    url = "http://[" + pump_ip + "]/v1/pumps/0"
    print("http out: " + url)
    try:
        response = requests.put(url,
                                json = { "activated": pump_on,
                                         "set_speed": 100 },
                                timeout = 2)
    except requests.exceptions.ConnectTimeout:
        print("HTTP request timeout")
        return
    except requests.exceptions.ConnectionError:
        print("HTTP request error")
        return
    if response.status_code != 200:
        print "HTTP response", response.status_code
        raise ValueError

def gpio_init():
    if not os.access('/sys/class/gpio/', os.W_OK):
        return
    for num in [led_gpio, user_button_gpio, low_level_gpio, high_level_gpio]:
        if not os.path.isdir("/sys/class/gpio/gpio"+str(num)):
            with open('/sys/class/gpio/export', 'w') as file:
                file.write(str(num))
        with open('/sys/class/gpio/gpio'+str(num)+'/direction', 'w') as file:
            if num == led_gpio:
                file.write("out")
            else:
                file.write("in")

def gpio_read():
    global user_button_pressed, above_low_level, above_high_level

    for num in [user_button_gpio, low_level_gpio, high_level_gpio]:
        with open('/sys/class/gpio/gpio'+str(num)+'/value', 'r') as file:
            val = int(file.read(1))
        if num == user_button_gpio:
            user_button_pressed = 1 - val
        elif num == low_level_gpio:
            above_low_level = val
        elif num == high_level_gpio:
            above_high_level = val

#### main ###

gpio_init()
mqtt_init()

try:
    while True:
        gpio_read()
        #print "button %d, low %d, high %d" % (user_button_pressed, above_low_level, above_high_level)

        if above_high_level or user_button_pressed:
            if not pump_running:
                pump_start(True)
        elif not above_low_level:
            if pump_running:
                pump_start(False)
        time.sleep(0.1)

except:
    print("exiting... ")
    mqtt.disconnect()
    mqtt.loop_stop()
    raise
