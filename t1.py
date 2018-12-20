import time
import paho.mqtt.client as paho
import json
import requests
import os

broker="digi"
pump_ip=""
mqtt = paho.Client("client-001")

def on_message(client, userdata, message):
    global pump_ip
    topic = message.topic.decode("utf-8")
    payload = message.payload.decode("utf-8")

    print(topic + ": " + payload)

    if topic == "node0/info":
        data = json.loads(payload)
        pump_ip = data["ip_addr"]
        print "node0 address is " + pump_ip

def mqtt_init():
    mqtt.on_message = on_message
    print("connecting")
    mqtt.connect(broker)
    mqtt.loop_start()
    print("subscribing")
    mqtt.subscribe("node0/#")


def pump_start(pump_on = True):
    if pump_ip == "":
        print "No pump in system"
        return
    url = "http://[" + pump_ip + "]/v1/pumps/0"
    print("Calling " + url)
    try:
        response = requests.put(url,
                                json = { "activated": pump_on,
                                         "set_speed": 100 },
                                timeout = 2)
    except requests.exceptions.ConnectTimeout:
        print("HTTP request timeout")
        return
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
    mqtt.disconnect()
    mqtt.loop_stop()
    raise
