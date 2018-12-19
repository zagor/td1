import time
import paho.mqtt.client as paho
import json
import requests
import os

broker="digi"
pump="pump"

def on_message(client, userdata, message):
    print(message.topic.decode("utf-8") + ": " + message.payload.decode("utf-8"))

def mqtt_init():
    client= paho.Client("client-001")
    client.on_message = on_message
    print("connecting")
    client.connect(broker)
    client.loop_start()
    print("subscribing")
    client.subscribe("node0/#")


def pump_start(pump_on = True):
    url = "http://" + pump + "/v1/pumps/0"
    print("Calling " + url)
    response = requests.put(url,
                            json = { "activated": pump_on,
                                     "set_speed": 100 })
    if response.status_code != 200:
        print "HTTP response", response.status_code
        raise ValueError

def led_init():
    if not os.access('/sys/class/gpio/', os.W_OK):
        return
    if not os.path.isdir("/sys/class/gpio/gpio75"):
        with open('/sys/class/gpio/export', 'w') as file:
            file.write("75")
    with open('/sys/class/gpio/gpio75/direction', 'w') as file:
            file.write("out")

def led(on=True):
    if not os.access('/sys/class/gpio/', os.W_OK):
        return
    with open('/sys/class/gpio/gpio75/value', 'w') as file:
        file.write("%d" % on*1)


#### main ###

led_init()
mqtt_init()

try:
    while True:
        time.sleep(2)
        led(True)
        pump_start(True)
        time.sleep(2)
        pump_start(False)
        led(False)

except:
    print("exiting... ")
    client.disconnect()
    client.loop_stop()
    raise
