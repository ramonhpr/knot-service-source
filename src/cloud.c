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
#define AMQP_QUEUE_CLOUD "cloud-messages"

/* Exchanges */
#define AMQP_EXCHANGE_FOG "fog"
#define AMQP_EXCHANGE_CLOUD "cloud"

 /* Southbound traffic (commands) */
#define AMQP_EVENT_DATA_UPDATE "data.update"
#define AMQP_EVENT_DATA_REQUEST "data.request"
#define AMQP_EVENT_DEVICE_UNREGISTERED "device.unregistered"
#define AMQP_EVENT_DEVICE_REGISTERED "device.registered"
#define AMQP_EVENT_DEVICE_LIST "device.list"

 /* Northbound traffic (control, measurements) */
#define AMQP_CMD_DATA_PUBLISH "data.publish"
#define AMQP_CMD_DEVICE_UNREGISTER "device.unregister"
#define AMQP_CMD_DEVICE_REGISTER "device.register"
#define AMQP_CMD_DEVICE_LIST "device.cmd.list"


enum cloud_events {
	update_evt,
	request_evt,
	removed_evt,
	added_evt,
	listed_evt
};

static struct l_hashmap *map_event_to_cb;

struct event_handler {
	void *cb;
	int event_id;
};

static const char *get_token_from_jobj(json_object *jso)
{
	json_object *jobjkey;

	if (!json_object_object_get_ex(jso, "token", &jobjkey))
		return false;

	if (json_object_get_type(jobjkey) != json_type_string)
		return false;

	return json_object_get_string(jobjkey);
}

static const char *get_id_from_json_str(json_object *jso)
{
	json_object *jobjkey;

	if (!json_object_object_get_ex(jso, "id", &jobjkey))
		return false;

	if (json_object_get_type(jobjkey) != json_type_string)
		return false;

	return json_object_get_string(jobjkey);
}

static bool cloud_receive_message(const char *exchange,
				  const char *routing_key,
				  const char *body, void *user_data)
{
	json_object *jso;
	struct l_queue *list;
	const char *id, *token;
	struct event_handler *handler;
	cloud_downstream_cb_t update_cb;
	cloud_device_removed_cb_t removed_cb;
	cloud_device_added_cb_t added_cb;
	cloud_devices_cb_t listed_cb;

	if (strcmp(AMQP_EXCHANGE_FOG, exchange) != 0)
		return true;

	handler = l_hashmap_lookup(map_event_to_cb, routing_key);
	if (!handler->cb)
		return false;

	jso = json_tokener_parse(body);
	if (!jso)
		return false;

	switch (handler->event_id) {
	case update_evt:
		update_cb = handler->cb;
		id = get_id_from_json_str(jso);
		list = parser_update_to_list(jso);
		if (list == NULL) {
			hal_log_error("Error on parse json object");
			json_object_put(jso);
			return false;
		}

		update_cb(id, list, user_data);
		l_queue_destroy(list, l_free);
		break;
	case request_evt:
		return true;
	case removed_evt:
		removed_cb = handler->cb;
		id = get_id_from_json_str(jso);
		removed_cb(id, user_data);
		break;
	case added_evt:
		added_cb = handler->cb;
		id = get_id_from_json_str(jso);
		token = get_token_from_jobj(jso);
		if (!token)
			return false;

		added_cb(id, token, user_data);
		break;
	case listed_evt:
		listed_cb = handler->cb;
		list = parser_mydevices_to_list(body);
		if (!l_queue_isempty(list))
			listed_cb(list, user_data);

		l_queue_destroy(list, NULL);
		l_hashmap_remove(map_event_to_cb,
					AMQP_EVENT_DEVICE_LIST);
		l_free(handler);
		break;
	default:
		hal_log_error("Unknown event %s", routing_key);
		break;
	}

	json_object_put(jso);
	return true;
}

int cloud_list_devices(cloud_devices_cb_t on_listed)
{
	amqp_bytes_t queue_cloud;
	json_object *jobj_empty;
	const char *json_str;
	const struct event_handler handler = {
		.cb = on_listed,
		.event_id = listed_evt
	};
	int result;

	l_hashmap_insert(map_event_to_cb, AMQP_EVENT_DEVICE_LIST,
				l_memdup(&handler, sizeof(handler)));
	jobj_empty = json_object_new_object();
	json_str = json_object_to_json_string(jobj_empty);

	queue_cloud = amqp_declare_new_queue(AMQP_QUEUE_CLOUD);
	if (queue_cloud.bytes == NULL) {
		hal_log_error("Error on declare a new queue.\n");
		return -1;
	}

	result = amqp_publish_persistent_message(queue_cloud,
						 AMQP_EXCHANGE_CLOUD,
						 AMQP_CMD_DEVICE_LIST,
						 json_str);
	if (result < 0)
		result = KNOT_ERR_CLOUD_FAILURE;

	json_object_put(jobj_empty);
	amqp_bytes_free(queue_cloud);

	return result;
}

int cloud_register_device(const char *id, const char *name)
{
	amqp_bytes_t queue_cloud;
	json_object *jobj_device;
	const char *json_str;
	int result;

	jobj_device = parser_device_json_create(id, name);
	json_str = json_object_to_json_string(jobj_device);

	queue_cloud = amqp_declare_new_queue(AMQP_QUEUE_CLOUD);
	if (queue_cloud.bytes == NULL) {
		hal_log_error("Error on declare a new queue.\n");
		return -1;
	}


	result = amqp_publish_persistent_message(queue_cloud,
						 AMQP_EXCHANGE_CLOUD,
						 AMQP_CMD_DEVICE_REGISTER,
						 json_str);
	if (result < 0)
		result = KNOT_ERR_CLOUD_FAILURE;

	json_object_put(jobj_device);
	amqp_bytes_free(queue_cloud);

	return result;
}

int cloud_unregister_device(const char *id)
{
	amqp_bytes_t queue_cloud;
	json_object *jobj;
	const char *json_str;
	int result;

	jobj = json_object_new_object();
	json_object_object_add(jobj, "id", json_object_new_string(id));
	json_str = json_object_to_json_string(jobj);

	queue_cloud = amqp_declare_new_queue(AMQP_QUEUE_CLOUD);
	if (queue_cloud.bytes == NULL) {
		hal_log_error("Error on declare a new queue.\n");
		return -1;
	}

	result = amqp_publish_persistent_message(queue_cloud,
		AMQP_EXCHANGE_CLOUD, AMQP_CMD_DEVICE_UNREGISTER, json_str);
	if (result < 0)
		return KNOT_ERR_CLOUD_FAILURE;

	json_object_put(jobj);
	amqp_bytes_free(queue_cloud);

	return 0;
}

int cloud_publish_data(const char *id, uint8_t sensor_id, uint8_t value_type,
		       const knot_value_type *value,
		       uint8_t kval_len)
{
	amqp_bytes_t queue_cloud;
	json_object *jobj_data;
	const char *json_str;
	int result;

	jobj_data = parser_data_create_object(id, sensor_id, value_type, value,
				       kval_len);
	json_str = json_object_to_json_string(jobj_data);

	queue_cloud = amqp_declare_new_queue(AMQP_QUEUE_CLOUD);
	if (queue_cloud.bytes == NULL) {
		hal_log_error("Error on declare a new queue.\n");
		return -1;
	}

	result = amqp_publish_persistent_message(queue_cloud,
						 AMQP_EXCHANGE_CLOUD,
						 AMQP_CMD_DATA_PUBLISH,
						 json_str);
	if (result < 0)
		result = KNOT_ERR_CLOUD_FAILURE;

	json_object_put(jobj_data);
	amqp_bytes_free(queue_cloud);
	return result;
}

int cloud_set_cbs(cloud_downstream_cb_t on_update,
		  cloud_downstream_cb_t on_request,
		  cloud_device_removed_cb_t on_removed,
		  cloud_device_added_cb_t on_added,
		  void *user_data)
{
	static const char * const fog_events[] = {
		AMQP_EVENT_DATA_UPDATE,
		AMQP_EVENT_DATA_REQUEST,
		AMQP_EVENT_DEVICE_UNREGISTERED,
		AMQP_EVENT_DEVICE_REGISTERED,
		AMQP_EVENT_DEVICE_LIST,
		NULL
	};
	const struct event_handler handlers[] = {
		{ .cb = on_update, .event_id = listed_evt },
		{ .cb = on_request, .event_id = request_evt },
		{ .cb = on_removed, .event_id = removed_evt },
		{ .cb = on_added, .event_id = added_evt },
		{ .cb = NULL, .event_id = listed_evt },
	};
	amqp_bytes_t queue_fog;
	int err, i;

	map_event_to_cb = l_hashmap_string_new();

	queue_fog = amqp_declare_new_queue(AMQP_QUEUE_FOG);
	if (queue_fog.bytes == NULL) {
		hal_log_error("Error on declare a new queue.\n");
		return -1;
	}

	for (i = 0; fog_events[i] != NULL; i++) {
		if (handlers[i].cb)
			l_hashmap_insert(map_event_to_cb, fog_events[i],
					 l_memdup(handlers + i,
						  sizeof(handlers[i])));
		err = amqp_set_queue_to_consume(queue_fog, AMQP_EXCHANGE_FOG,
					fog_events[i]);
		if (err) {
			hal_log_error("Error on set up queue to consume.\n");
			return -1;
		}
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
	l_hashmap_destroy(map_event_to_cb, l_free);
	amqp_stop();
}
