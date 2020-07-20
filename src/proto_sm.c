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
KnotMsgRegisterRsp_Closure closure_reg;
void *reg_user_data;
bool auth_inner_call;
char *cache_token;

static void to_sensor(void *data, void *user_data)
{
	Thing *new_thing = user_data;
	int i = new_thing->schema->n_schema;

	new_thing->schema[i].schema = data;
	new_thing->schema->n_schema++;
}

static void to_thing(void *data, void *user_data)
{
	struct cloud_device *dev = data;
	Thing *new_thing = l_new(Thing, 1);
	ThingList *list = user_data;

	new_thing->id = dev->id;
	new_thing->name = dev->name;
	new_thing->online = dev->online;
	new_thing->schema = l_new(Schema, 1);
	l_queue_foreach(dev->schema, to_sensor, new_thing);
	list->things[list->n_things++] = new_thing;
}

static struct l_queue *schema_to_queue(Schema schema)
{
	return NULL;
}

static bool handle_cloud_msg_list(struct l_queue *devices, const char *err)
{
	ThingList *list;
	size_t len = l_queue_length(devices);

	if (err) {
		hal_log_error("Received List devices error: %s", err);
		closure_list(NULL, NULL);
		return true;
	}

	list = l_new(ThingList, 1);
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

static bool handle_cloud_msg_register(const char *device_id,
				const char *token, const char *error)
{
	KnotMsgRegisterRsp msg = KNOT_MSG_REGISTER_RSP__INIT;

	if (error) {
		msg.result = KNOT_STATUS__ERROR_UNKNOWN;
		closure_reg(&msg, reg_user_data);
		return false;
	}

	auth_inner_call = true;
	cache_token = l_strdup(token);
	if (cloud_auth_device(device_id, token) < 0) {
		msg.result = KNOT_STATUS__CLOUD_OFFLINE;
		closure_reg(&msg, reg_user_data);
		return false;
	}

	return true;
}


static bool handle_inner_auth(const char *device_id, const char *error)
{
	KnotMsgRegisterRsp msg = KNOT_MSG_REGISTER_RSP__INIT;

	if (error) {
		msg.result = KNOT_STATUS__CREDENTIAL_UNAUTHORIZED;
		closure_reg(&msg, reg_user_data);
		return false;
	}

	auth_inner_call = false;
	msg.uuid = (char *)device_id;
	msg.token = (char *)cache_token;
	msg.result = KNOT_STATUS__SUCCESS;
	closure_reg(&msg, reg_user_data);
	l_free(cache_token);

	return true;
}

static bool handle_cloud_auth(const char *device_id, const char *error)
{
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
		return handle_cloud_msg_register(msg->device_id, msg->token,
						msg->error);
	case AUTH_MSG:
		if (auth_inner_call)
			return handle_inner_auth(msg->device_id, msg->error);
		else
			return handle_cloud_auth(msg->device_id, msg->error);
	case SCHEMA_MSG:
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
	KnotMsgRegisterRsp rsp = KNOT_MSG_REGISTER_RSP__INIT;
	char id[KNOT_ID_LEN];

	snprintf(id, sizeof(id), "%016"PRIx64, input->id);
	if (cloud_register_device(id, input->name) < 0) {
		rsp.result = KNOT_STATUS__CLOUD_OFFLINE;
		closure(&rsp, closure_data);
	}

	reg_user_data = closure_data;
	closure_reg = closure;
}

static void proto_sm_list_devices(KnotSm_Service *service,
					   const Empty *input,
					   ThingList_Closure closure,
					   void *closure_data)
{
	if (cloud_list_devices() < 0)
		closure(NULL, closure_data);

	closure_list = closure;
}

static void proto_sm_schema(KnotSm_Service *service,
				const KnotMsgSchemaReq *input,
				KnotMsgSchemaRsp_Closure closure,
				void *closure_data)
{
	cloud_update_schema(input->uuid, schema_to_queue());
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
