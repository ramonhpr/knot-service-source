/*
 * This file is part of the KNOT Project
 *
 * Copyright (c) 2019, CESAR. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdbool.h>
#include <stdint.h>
#include <ell/ell.h>
#include <json-c/json.h>
#include <hal/linux_log.h>
#include <amqp.h>

#include <knot/knot_protocol.h>

#include "settings.h"
#include "amqp.h"
#include "parser.h"
#include "cloud.h"

#define AMQP_QUEUE_FOG "fog-messages"

/* Exchanges */
#define AMQP_EXCHANGE_FOG "fog"
#define AMQP_EXCHANGE_CLOUD "cloud"

 /* Southbound traffic (commands) */
#define AMQP_EVENT_DATA_UPDATE "data.update"
#define AMQP_EVENT_DATA_REQUEST "data.request"

 /* Northbound traffic (control, measurements) */
#define AMQP_CMD_DATA_PUBLISH "data.publish"
#define AMQP_CMD_DEVICE_UNREGISTER "device.unregister"

struct cloud_callbacks {
	cloud_downstream_cb_t update_cb;
	cloud_downstream_cb_t request_cb;
};

static struct cloud_callbacks cloud_cbs;

static bool cloud_receive_message(const char *exchange,
				  const char *routing_key,
				  const char *body, void *user_data)
{
	json_object *jso;
	json_object *id_json;
	struct l_queue *list;
	const char *id;

	if (strcmp(AMQP_EXCHANGE_FOG, exchange) != 0)
		return true;

	jso = json_tokener_parse(body);
	if (!jso)
		return false;

	if (!json_object_object_get_ex(jso, "id", &id_json))
		return false;

	if (json_object_get_type(id_json) != json_type_string)
		return false;

	id = json_object_get_string(id_json);

	if (cloud_cbs.update_cb != NULL &&
	    strcmp(AMQP_EVENT_DATA_UPDATE, routing_key) == 0) {
		list = parser_update_to_list(jso);
		if (list == NULL) {
			hal_log_error("Error on parse json object");
			json_object_put(jso);
			return false;
		}

		cloud_cbs.update_cb(id, list, user_data);
		l_queue_destroy(list, l_free);
	}

	if (cloud_cbs.request_cb != NULL &&
	    strcmp(AMQP_EVENT_DATA_REQUEST, routing_key) == 0) {
		return true;
		/* Call cloud_cbs.request_cb */
	}

	json_object_put(jso);
	return true;
}

int cloud_unregister_device(const char *id)
{
	json_object *jobj;
	const char *json_str;
	int result;

	jobj = json_object_new_object();
	json_object_object_add(jobj, "id", json_object_new_string(id));
	json_str = json_object_to_json_string(jobj);

	result = amqp_publish_persistent_message(
		AMQP_EXCHANGE_CLOUD, AMQP_CMD_DEVICE_UNREGISTER, json_str);
	if (result < 0)
		return KNOT_ERR_CLOUD_FAILURE;

	json_object_put(jobj);

	return 0;
}

int cloud_publish_data(const char *id, uint8_t sensor_id, uint8_t value_type,
		       const knot_value_type *value,
		       uint8_t kval_len)
{
	json_object *jobj_data;
	const char *json_str;
	int result;

	jobj_data = parser_data_create_object(id, sensor_id, value_type, value,
				       kval_len);
	json_str = json_object_to_json_string(jobj_data);
	result = amqp_publish_persistent_message(AMQP_EXCHANGE_CLOUD,
						 AMQP_CMD_DATA_PUBLISH,
						 json_str);
	if (result < 0)
		result = KNOT_ERR_CLOUD_FAILURE;

	json_object_put(jobj_data);
	return result;
}

int cloud_set_cbs(cloud_downstream_cb_t on_update,
		  cloud_downstream_cb_t on_request,
		  void *user_data)
{
	amqp_bytes_t queue_fog;
	int err;

	cloud_cbs.update_cb = on_update;
	cloud_cbs.request_cb = on_request;

	queue_fog = amqp_declare_new_queue(AMQP_QUEUE_FOG);
	if (queue_fog.bytes == NULL) {
		hal_log_error("Error on declare a new queue.\n");
		return -1;
	}

	err = amqp_set_queue_to_consume(queue_fog, AMQP_EXCHANGE_FOG,
					AMQP_EVENT_DATA_UPDATE);
	if (err) {
		hal_log_error("Error on set up queue to consume.\n");
		return -1;
	}

	err = amqp_set_queue_to_consume(queue_fog, AMQP_EXCHANGE_FOG,
					AMQP_EVENT_DATA_REQUEST);
	if (err) {
		hal_log_error("Error on set up queue to consume.\n");
		return -1;
	}

	amqp_bytes_free(queue_fog);

	err = amqp_set_read_cb(cloud_receive_message, user_data);
	if (err) {
		hal_log_error("Error on set up read callback\n");
		return -1;
	}

	return 0;
}

int cloud_start(struct settings *settings)
{
	return amqp_start(settings);
}

void cloud_stop(void)
{
	amqp_stop();
}
