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

static struct thing_conn *session_ref(struct thing_conn *session)
{
	if (unlikely(!session))
		return NULL;

	__sync_fetch_and_add(&session->refs, 1);
	hal_log_info("thing_conn_ref(%p): %d", session, session->refs);

	return session;
}

static void session_unref(struct thing_conn *session)
{
	//TODO
}

static struct l_io *create_node_channel(int node_socket,
					struct thing_conn *session)
{
	// TODO
	return 0;
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
		session_unref(session);
		return NULL;
	}
	session->node_fd = client_socket; /* Required to manage disconnections */

	hal_log_info("[session %p] thing connection created", session);

	return session_ref(session);
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
