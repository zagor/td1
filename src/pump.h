#pragma once

#include <json.h>

struct pump {
	bool activated;
	int set_speed;
};

void pump_data_updated(void);
void pump_data_wait(void);
int pump_consume_json(char *json, int len);
int pump_produce_json(char *outbuf, int len);
struct pump *pump_data_get(void);
struct json_obj_descr *pump_json_get(void);
int pump_init(void);
