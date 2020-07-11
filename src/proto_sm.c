#include <knot/knot.pb-c.h>
#include <ell/ell.h>
#include <stdio.h>
#include <hal/linux_log.h>
#include "settings.h"
#include <knot/knot_protocol.h>
#include "cloud.h"
#include "proto_sm.h"

#define KNOT_ID_LEN 17
#define KNOT_PDU_LEN 20

ThingList_Closure closure_list;

static void to_sensor(void *data, void *user_data)
{
	Thing *new_thing = user_data;
	int i = new_thing->schema->n_schema_frags;

	new_thing->schema[i].schema_frags = data;
	new_thing->schema->n_schema_frags++;
}

static void to_thing(void *data, void *user_data)
{
	struct cloud_device *dev = data;
	Thing *new_thing = l_new(Thing, 1);
	ThingList *list = user_data;

	new_thing->id = dev->id;
	new_thing->name = dev->name;
	new_thing->online = dev->online;
	new_thing->schema = l_new(KnotMsgSchema, 1);
	l_queue_foreach(dev->schema, to_sensor, new_thing);
	list->things[list->n_things++] = new_thing;
}

static bool handle_cloud_msg_list(struct l_queue *devices, const char *err)
{
	size_t len = l_queue_length(devices);
	ThingList *list = l_new(ThingList, 1);

	if (err) {
		hal_log_error("Received List devices error: %s", err);
		return true;
	}

	list->n_things = 0;
	list->things = l_malloc(sizeof(Thing *) * len);
	l_queue_foreach(devices, to_thing, list);

	closure_list(list, NULL);

	for (size_t i = 0; i < len; i++) {
		l_free(list->things[i]->schema);
		l_free(list->things[i]);
	}
	l_free(list->things);
	l_free(list);

	return true;
}

static bool on_cloud_receive(const struct cloud_msg *msg, void *user_data)
{
	switch (msg->type) {
	case LIST_MSG:
		return handle_cloud_msg_list(msg->list, msg->error);
	case UPDATE_MSG:
	case REQUEST_MSG:
	case REGISTER_MSG:
	case SCHEMA_MSG:
	case AUTH_MSG:
	case UNREGISTER_MSG:
	default:
		return true;
	}
}

static void when_connected(void *user_data)
{
	cloud_connected_cb_t cb = user_data;
	int err;

	hal_log_info("Cloud CONNECTED");
	err = cloud_set_read_handler(on_cloud_receive, user_data);
	if (err < 0) {
		hal_log_error("cloud_set_read_handler(): %s", strerror(-err));
		return;
	}

	cb(NULL);
}

static void proto_sm_register_thing(KnotSm_Service *service,
						const KnotMsgRegisterReq *input,
						KnotMsgRegisterRsp_Closure closure,
						void *closure_data)
{
	char id[KNOT_ID_LEN];
	KnotMsgRegisterRsp output = KNOT_MSG_REGISTER_RSP__INIT;

	snprintf(id, sizeof(id), "%016"PRIx64, input->id);
	output.uuid = id;
	closure(&output, closure_data);
}

static void proto_sm_list_devices(KnotSm_Service *service,
					   const Empty *input,
					   ThingList_Closure closure,
					   void *closure_data)
{
	cloud_list_devices();
	closure_list = closure;
}

KnotSm_Service sm = KNOT_SM__INIT(proto_sm_);

KnotSm_Service *proto_sm_get(void)
{
	return &sm;
}

int proto_sm_start(struct settings *settings, cloud_connected_cb_t cb)
{
	int err = cloud_start(settings, when_connected, cb);

	if (err < 0)
		return err;

	return 0;
}

void proto_sm_stop(void)
{
	cloud_stop();
}
