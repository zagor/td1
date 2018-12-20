#include <net/socket.h>
#include "mqtt.h"
#include "http.h"
#include "pump.h"

void main(void)
{
	mqtt_start();
	http_start();
	pump_init();
}
