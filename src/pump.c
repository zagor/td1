#include <zephyr.h>
#include <device.h>
#include <gpio.h>
#include "pump.h"

struct pump pump_data;
static struct device *gpioe;

static const struct json_obj_descr pump_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct pump, activated, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct pump, set_speed, JSON_TOK_NUMBER),
};

K_SEM_DEFINE(data_sem, 1, 1);	/* starts off "available" */

void pump_data_updated(void)
{
	k_sem_give(&data_sem);
}

void pump_data_wait(void)
{
	k_sem_take(&data_sem, K_FOREVER);
}

struct pump *pump_data_get(void)
{
	return &pump_data;
}

void pump_action(void)
{
	gpio_pin_write(gpioe, 9, pump_data.activated);
}

int pump_consume_json(char *json, int len)
{
	int ret;

	ret = json_obj_parse(json, len, pump_descr, ARRAY_SIZE(pump_descr),
			     &pump_data);
	if (ret < 0)
		return ret;

	pump_data_updated();
	pump_action();
	return 0;
}

int pump_produce_json(char *outbuf, int len)
{
	int ret = json_obj_encode_buf(pump_descr, ARRAY_SIZE(pump_descr),
				      &pump_data, outbuf, len);
	return ret;
}

int pump_init(void)
{
	gpioe = device_get_binding("GPIOE");
	if (!gpioe) {
          printk("error line %d\n", __LINE__);
          return -EINVAL;
	}

	gpio_pin_configure(gpioe, 9, GPIO_DIR_OUT);
	return 0;
}
