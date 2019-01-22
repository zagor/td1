#include <zephyr.h>
#include <net/socket.h>
#include <net/mqtt.h>

#include "pump.h"

#define MQTT_PORT 1883 /* plain text */
#define MQTT_SERVER "2001:1002::1" /* todo: auto detect */
#define MQTT_CLIENTID "pump_module"
#define MQTT_BUFFER_SIZE 128
#define STACKSIZE 1024
#define CONNECT_TIMEOUT 5000 // ms
#define MQTT_PING_TIME  30000 // ms
#define INPUT_THREAD_DELAY 1000 // ms
#define OUTPUT_THREAD_DELAY 2000 // ms

#define TOPIC_NODE0_INFO "node0/info"
#define WILL_MESSAGE "{\"ip_addr\":\"\"}"

/* The mqtt client struct */
static struct mqtt_client mqtt_client;

static bool connected = false;

static char node_ip_address[NET_IPV6_ADDR_LEN];
static char node_version[16];

struct node {
	char *ip_addr;
	char *version;
} node_data = {
	.ip_addr = node_ip_address,
	.version = node_version
};

static const struct json_obj_descr node_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct node, ip_addr, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct node, version, JSON_TOK_STRING),
};

void mqtt_publish_topic(char *topic, char *payload, bool retain)
{
	struct mqtt_publish_param param;
        const enum mqtt_qos qos = MQTT_QOS_0_AT_MOST_ONCE;

	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = topic;
	param.message.topic.topic.size = strlen(topic);
	param.message.payload.data = payload;
	param.message.payload.len = strlen(payload);
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = retain ? 1 : 0;

	int rc = mqtt_publish(&mqtt_client, &param);
        if (rc < 0)
                printk("mqtt_publish %d\n", rc);
	printk("mqtt out: %s: %s\n", topic, payload);
}

static void announce_node(void)
{
        char jsonbuf[80];
	struct net_if *iface = net_if_get_default();

        net_addr_ntop(AF_INET6,
                      &iface->config.ip.ipv6->unicast[1].address.in6_addr,
                      node_data.ip_addr, sizeof node_data.ip_addr);
        printk("Node IP address: %s\n", node_data.ip_addr);

	strcpy(node_data.version, "0.1");

	int ret = json_obj_encode_buf(node_descr, ARRAY_SIZE(node_descr),
				      &node_data, jsonbuf, sizeof jsonbuf);
	if (ret < 0) {
		printk("mqtt: produce json failed\n");
		return;
	}
	mqtt_publish_topic(TOPIC_NODE0_INFO, jsonbuf, true);
}

static void event_handler(struct mqtt_client *const client,
                          const struct mqtt_evt *event)
{
	switch (event->type) {
                case MQTT_EVT_CONNACK:
                        printk("MQTT connected\n");
                        connected = true;
			announce_node();
                        break;

                case MQTT_EVT_DISCONNECT:
                        printk("MQTT disconnected\n");
                        connected = false;
                        break;

                default:
                        break;
        }
}

static void configure_client(struct mqtt_client *client)
{
        static u8_t rx_buffer[MQTT_BUFFER_SIZE];
        static u8_t tx_buffer[MQTT_BUFFER_SIZE];
        static struct sockaddr_storage mqtt_broker;
	static struct mqtt_topic will_topic = {
		.topic = {
			.utf8 = TOPIC_NODE0_INFO,
			.size = sizeof TOPIC_NODE0_INFO
		},
		.qos = MQTT_QOS_1_AT_LEAST_ONCE
	};
	static struct mqtt_utf8 will_message = {
		.utf8 = WILL_MESSAGE,
		.size = sizeof WILL_MESSAGE
	};

        /* set broker address */
	struct sockaddr_in6 *broker6 = (struct sockaddr_in6 *)&mqtt_broker;
	broker6->sin6_family = AF_INET6;
	broker6->sin6_port = htons(MQTT_PORT);
	inet_pton(AF_INET6, MQTT_SERVER, &broker6->sin6_addr);

        /* reset mqtt client struct */
	mqtt_client_init(client);

	/* MQTT client configuration */
	client->broker = &mqtt_broker;
	client->evt_cb = event_handler;
	client->client_id.utf8 = (u8_t *)MQTT_CLIENTID;
	client->client_id.size = strlen(MQTT_CLIENTID);
	client->password = NULL;
	client->user_name = NULL;
	client->protocol_version = MQTT_VERSION_3_1_1;
	client->will_topic = &will_topic;
	client->will_message = &will_message;
	client->will_retain = false;

	/* MQTT buffers configuration */
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	/* MQTT transport configuration */
#if defined(CONFIG_MQTT_LIB_TLS)
	client->transport.type = MQTT_TRANSPORT_SECURE;

	struct mqtt_sec_config *tls_config = &client->transport.tls.config;

	tls_config->peer_verify = 2;
	tls_config->cipher_list = NULL;
	tls_config->seg_tag_list = m_sec_tags;
	tls_config->sec_tag_count = ARRAY_SIZE(m_sec_tags);
# if defined(MBEDTLS_X509_CRT_PARSE_C)
	tls_config->hostname = TLS_SNI_HOSTNAME;
# else
	tls_config->hostname = NULL;
# endif
#else
	client->transport.type = MQTT_TRANSPORT_NON_SECURE;
#endif
}

static struct pollfd poll_socket;
static void prepare_fds(struct mqtt_client *client)
{
	if (client->transport.type == MQTT_TRANSPORT_NON_SECURE)
		poll_socket.fd = client->transport.tcp.sock;
#if defined(CONFIG_MQTT_LIB_TLS)
	else if (client->transport.type == MQTT_TRANSPORT_SECURE) {
		poll_socket.fd = client->transport.tls.sock;
#endif

	poll_socket.events = ZSOCK_POLLIN;
}

static void reconnect(void)
{
        while (!connected) {
                int rc;
                printk("mqtt_connect()\n");
                rc = mqtt_connect(&mqtt_client);
                if (rc < 0) {
			printk("mqtt_connect %d\n", rc);
                        k_sleep(1000);
                        continue;
                }

                /* wait for response */
                prepare_fds(&mqtt_client);
                poll(&poll_socket, 1, CONNECT_TIMEOUT);

                printk("mqtt_input()\n");
                rc = mqtt_input(&mqtt_client);
                if (rc < 0)
                        printk("mqtt_input %d\n", rc);

                if (connected)
                        break;

                printk("mqtt is not connected\n");
                mqtt_abort(&mqtt_client);
                k_sleep(1000); // back off a second
        }
}

static void input_loop(void)
{
        configure_client(&mqtt_client);

        while (1) {
                int rc;

                if (!connected)
                        reconnect();

                if (poll(&poll_socket, 1, CONNECT_TIMEOUT)) {
                        rc = mqtt_input(&mqtt_client);
                        if (rc != 0) {
                                printk("mqtt_input %d\n", rc);
                                continue;
                        }
                }

                /* ping */
                rc = mqtt_live(&mqtt_client);
                if (rc < 0)
                        printk("mqtt_live failed %d\n", rc);

        }
}

static struct pump last_pump_data;

static void output_loop(void)
{
        struct pump *pump_data = pump_data_get();
	memset(&last_pump_data, 0xff, sizeof last_pump_data);

        while (1) {
                /* todo: make generic */
                if (last_pump_data.activated != pump_data->activated) {
                        char buf[8];
                        snprintf(buf, sizeof buf, "%d", pump_data->activated);
                        mqtt_publish_topic("node0/pump/activated", buf, true);
                }

                if (last_pump_data.set_speed != pump_data->set_speed) {
                        char buf[8];
                        snprintf(buf, sizeof buf, "%d", pump_data->set_speed);
                        mqtt_publish_topic("node0/pump/set_speed", buf, true);
                }
                last_pump_data = *pump_data;
                pump_data_wait();
        }
}

/* thread */
__kernel struct k_thread input_thread;
K_THREAD_STACK_DEFINE(input_stack, STACKSIZE);

__kernel struct k_thread output_thread;
K_THREAD_STACK_DEFINE(output_stack, STACKSIZE);

void mqtt_start(void)
{
        k_thread_create(&input_thread, input_stack, sizeof input_stack,
                        (k_thread_entry_t)input_loop,
                        NULL, NULL, NULL, -1, K_USER, INPUT_THREAD_DELAY);

	k_thread_create(&output_thread, output_stack, sizeof output_stack,
                        (k_thread_entry_t)output_loop,
                        NULL, NULL, NULL, -1, K_USER, OUTPUT_THREAD_DELAY);
}
