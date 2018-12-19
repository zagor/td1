#include <net/socket.h>
#include "mqtt.h"
#include "http.h"
#include "pump.h"

void print_ip(void)
{
	struct net_if *iface;
        char buf[NET_IPV6_ADDR_LEN];
	iface = net_if_get_default();
        net_addr_ntop(AF_INET6,
                      &iface->config.ip.ipv6->unicast[1].address.in6_addr,
                      buf, sizeof(buf));
        printk("My IP address: %s\n", buf);
}

void main(void)
{
	print_ip();
	mqtt_start();
	http_start();
	pump_init();
}
