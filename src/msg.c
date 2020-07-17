/*
 * This file is part of the KNOT Project
 *
 * Copyright (c) 2018, CESAR. All rights reserved.
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

#ifndef  _GNU_SOURCE
#define  _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include <ell/ell.h>

#include <json-c/json.h>

#include <knot/knot.pb-c.h>
#include <hal/linux_log.h>

#include "settings.h"
#include "node.h"
#include "device.h"
#include "proxy.h"
#include "msg.h"
#include "cloud.h"
#include "proto_sm.h"

#define TIMEOUT_DEVICES_SEC	3 /* Time waiting to request for devices */

struct thing_conn {
	int refs;
	struct node_ops *node_ops;
	struct l_io *node_channel;	/* Radio event source */
	char *id;
	int node_fd;			/* Unix socket */
};
bool node_enabled;
struct l_queue *connections;
static struct l_timeout *list_timeout;

static struct thing_conn *thing_conn_ref(struct thing_conn *session)
{
	if (unlikely(!session))
		return NULL;

	__sync_fetch_and_add(&session->refs, 1);
	hal_log_info("thing_conn_ref(%p): %d", session, session->refs);

	return session;
}

static void session_destroy(struct thing_conn *session)
{
	if (unlikely(!session))
		return;

	l_io_destroy(session->node_channel);

	if (session->id != NULL)
		l_free(session->id);

	l_free(session);
}

static void thing_conn_unref(struct thing_conn *session)
{
	if (unlikely(!session))
		return;

	hal_log_info("session_unref(%p): %d", session, session->refs - 1);
	if (__sync_sub_and_fetch(&session->refs, 1))
		return;

	session_destroy(session);
}

static void handle_device_added(const KnotMsgRegisterRsp *reg_msg,
						void *closure_data)
{
	struct thing_conn *session = closure_data;
	uint8_t *out;
	int osent, err;
	size_t olen;

	olen = knot_msg_register_rsp__get_packed_size(reg_msg);
	out = l_malloc(olen);
	knot_msg_register_rsp__pack(reg_msg, out);
	hal_log_dbg("device added %s %s. Sending %ld bytes", reg_msg->uuid,
							reg_msg->token, olen);

	osent = session->node_ops->send(session->node_fd, out, olen);
	if (osent < 0) {
		err = -osent;
		hal_log_error("[session %p] Can't send register response %s(%d)"
			      , session, strerror(err), err);
	} else if (reg_msg->result == KNOT_STATUS__SUCCESS) {
		// TODO: start schema roolback
	}

	l_free(out);
}

static ssize_t msg_process(struct thing_conn *session,
				const void *ipdu, size_t ilen,
				void *opdu, size_t omtu)
{
	KnotMsg *msg = knot_msg__unpack(NULL, ilen, ipdu);

	switch (msg->msg_case) {
	case KNOT_MSG__MSG_REG_REQ:
		proto_sm_get()->register_thing(proto_sm_get(), msg->reg_req,
						handle_device_added, session);
		break;
	case KNOT_MSG__MSG__NOT_SET:
	case _KNOT_MSG__MSG_IS_INT_SIZE:
	case KNOT_MSG__MSG_REG_RSP:
		break;
	default:
		break;
	}

	knot_msg__free_unpacked(msg, NULL);
	return 0;
}

static void session_node_destroy_to(struct l_timeout *timeout,
					    void *user_data)
{
	struct l_io *channel = user_data;

	l_io_destroy(channel);
}

static void on_node_channel_data_error(struct l_io *channel)
{
	static bool destroying = false;

	if (destroying)
		return;

	destroying = true;

	l_timeout_create(1,
			 session_node_destroy_to,
			 channel,
			 NULL);
}

static bool session_node_data_cb(struct l_io *channel, void *user_data)
{
	struct thing_conn *session = user_data;
	struct node_ops *node_ops = session->node_ops;
	uint8_t ipdu[512], opdu[512]; /* FIXME: */
	ssize_t recvbytes, sentbytes, olen;
	int node_socket;
	int err;

	node_socket = l_io_get_fd(channel);

	recvbytes = node_ops->recv(node_socket, ipdu, sizeof(ipdu));
	if (recvbytes <= 0) {
		err = errno;
		hal_log_error("[session %p] readv(): %s(%d)",
			      session, strerror(err), err);
		on_node_channel_data_error(channel);
		return false;
	}

	/* Blocking: Wait until response from cloud is received */
	olen = msg_process(session, ipdu, recvbytes, opdu, sizeof(opdu));
	/* olen: output length or -errno */
	if (olen < 0) {
		/* Server didn't reply any error */
		hal_log_error("[session %p] KNOT IoT cloud error: %s(%zd)",
			      session, strerror(-olen), -olen);
		return true;
	}

	/* If there are no octets to be sent */
	if (!olen)
		return true;

	/* Response from the gateway: error or response for the given command */
	sentbytes = node_ops->send(node_socket, opdu, olen);
	if (sentbytes < 0)
		hal_log_error("[session %p] node_ops: %s(%zd)",
			      session, strerror(-sentbytes), -sentbytes);

	return true;
}

static void session_node_disconnected_cb(struct l_io *channel, void *user_data)
{

}

static void session_node_destroy_cb(void *user_data)
{
	struct thing_conn *session = user_data;

	thing_conn_unref(session);
}

static struct l_io *create_node_channel(int node_socket,
					struct thing_conn *session)
{
	struct l_io *channel;

	channel = l_io_new(node_socket);
	if (channel == NULL) {
		hal_log_error("Can't create node channel");
		return NULL;
	}

	l_io_set_close_on_destroy(channel, true);

	l_io_set_read_handler(channel, session_node_data_cb,
			      session, NULL);
	l_io_set_disconnect_handler(channel,
				    session_node_disconnected_cb,
				    thing_conn_ref(session),
				    session_node_destroy_cb);

	return channel;
}

static struct thing_conn *thing_conn_new(struct node_ops *node_ops,
										 int client_socket)
{
	struct thing_conn *session;

	session = l_new(struct thing_conn, 1);
	session->refs = 0;
	session->id = NULL;
	session->node_ops = node_ops;
	session->node_channel = create_node_channel(client_socket, session);
	if (session->node_channel == NULL) {
		thing_conn_unref(session);
		return NULL;
	}
	session->node_fd = client_socket; /* Required to manage disconnections */

	hal_log_info("[session %p] thing connection created", session);

	return thing_conn_ref(session);
}

static bool on_acceptedd(struct node_ops *node_ops, int client_socket)
{
	struct thing_conn *conn = thing_conn_new(node_ops, client_socket);

	if (!conn) {
		/* FIXME: Stop knotd if cloud if not available */
		return false;
	}

	l_queue_push_head(connections, conn);
	return true;
}

static void handle_list_device(const ThingList *message, void *closure_data)
{
	if (!message) {
		hal_log_error("Received message null. Retrying to list");
		l_timeout_modify(list_timeout, TIMEOUT_DEVICES_SEC);
		return;
	}

	l_timeout_remove(list_timeout);
	list_timeout = NULL;
	for (size_t i = 0; i < message->n_things; i++) {
		Thing *p = message->things[i];
		bool registered = p->schema->n_schema_frags > 0;
		struct knot_device* device_dbus;

		device_dbus = device_create(p->id, p->name, false, true, registered);
		device_set_uuid(device_dbus, p->id);
	}

	if (!node_enabled) {
		node_start(on_acceptedd);
		node_enabled = true;
	}
}

static void list_timeout_cb(struct l_timeout *timeout, void *user_data)
{
	KnotSm_Service *ptr = proto_sm_get();

	ptr->list_devices(ptr, NULL, handle_list_device, NULL);
}

static void when_connected(void *user_data)
{
	list_timeout = l_timeout_create_ms(1, /* start in oneshot */
				list_timeout_cb,
				NULL, NULL);
}

int msg_start(struct settings *settings)
{
	int err = device_start();
	if (err < 0) {
		hal_log_error("device_start(): %s", strerror(-err));
		return err;
	}

	connections = l_queue_new();

	err = proto_sm_start(settings, when_connected);
	if (err < 0) {
		hal_log_error("proto_sm_start(): %s", strerror(-err));
		return err;
	}

	return 0;
}

void msg_stop(void)
{
	if (node_enabled)
		node_stop();

	proto_sm_stop();
	device_stop();
}
