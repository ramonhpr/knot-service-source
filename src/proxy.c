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

#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <ell/ell.h>

#include "hal/linux_log.h"

#include "dbus.h"
#include "device.h"
#include "proxy.h"

struct proxy {
	unsigned int watch_id;
	struct l_dbus_client *client;
	char *name;
	char *path;
	char *interface;
};

static struct l_hashmap *device_list;
static struct proxy *proxy;

static void proxy_free(struct proxy *proxy)
{
	l_free(proxy->path);
	l_free(proxy->interface);
	l_free(proxy);
}

static void service_appeared(struct l_dbus *dbus, void *user_data)
{
	struct proxy *proxy = user_data;
	hal_log_info("Service appeared: %s", proxy->name);
}

static void service_disappeared(struct l_dbus *dbus, void *user_data)
{
	struct proxy *proxy = user_data;
	hal_log_info("Service disappeared: %s", proxy->name);
}

static void added(struct l_dbus_proxy *ellproxy, void *user_data)
{
	const char *interface = l_dbus_proxy_get_interface(ellproxy);
	const char *path = l_dbus_proxy_get_path(ellproxy);
	struct proxy *proxy = user_data;
	struct knot_device *device;
	uint64_t id;

	if (strcmp(interface, proxy->interface) != 0)
		return;

	/* Debug purpose only */
	hal_log_info("proxy added: %s %s", path, interface);

	/* FIXME: Use 'Id'  read from D-Bus instead of proxy address */
	id = L_PTR_TO_UINT(ellproxy);
	device = device_create(ellproxy, id, "device:unknown");
	l_hashmap_insert(device_list, ellproxy, device);
}

static void removed(struct l_dbus_proxy *ellproxy, void *user_data)
{
	const char *interface = l_dbus_proxy_get_interface(ellproxy);
	const char *path = l_dbus_proxy_get_path(ellproxy);
	struct proxy *proxy = user_data;
	struct knot_device *device;

	if (strcmp(interface, proxy->interface) != 0)
		return;

	/* Debug purpose only */
	hal_log_info("proxy removed: %s %s", path, interface);

	device = l_hashmap_remove(device_list, ellproxy);
	if (!device)
		return;

	device_destroy(device);
}

static void property_changed(struct l_dbus_proxy *ellproxy,
			     const char *propname, struct l_dbus_message *msg,
			     void *user_data)
{
	struct proxy *proxy = user_data;
	const char *path = l_dbus_proxy_get_path(ellproxy);
	const char *interface = l_dbus_proxy_get_interface(ellproxy);
	struct knot_device *device;
	const char *name;
	bool paired;

	if (strcmp(proxy->interface, interface) != 0)
		return;

	if (strcmp("Name", propname) == 0) {
		device = l_hashmap_lookup(device_list, ellproxy);
		if (!device)
			return;

		if (l_dbus_message_get_arguments(msg, "s", &name))
			device_set_name(device, name);

	} else if (strcmp("Paired", propname) == 0) {
		device = l_hashmap_lookup(device_list, ellproxy);
		if (!device)
			return;

		if (l_dbus_message_get_arguments(msg, "b", &paired))
			device_set_paired(device, paired);
	} else {
		/* Ignore other properties */
		return;
	}

	hal_log_info("property changed: %s (%s %s)", propname, path, interface);
}

static struct proxy *watch_create(const char *service,
			const char *path, const char *interface)
{
	struct proxy *proxy;

	proxy = l_new(struct proxy, 1);
	proxy->path = l_strdup(path);
	proxy->interface = l_strdup(interface);
	proxy->watch_id = l_dbus_add_service_watch(dbus_get_bus(), service,
						   service_appeared,
						   service_disappeared,
						   proxy, NULL);

	proxy->client = l_dbus_client_new(dbus_get_bus(), service, "/");
	l_dbus_client_set_proxy_handlers(proxy->client, added,
					 removed, property_changed,
					 proxy, NULL);

	return proxy;
}

static void watch_remove(struct proxy *proxy)
{
	l_dbus_client_destroy(proxy->client);
	l_dbus_remove_watch(dbus_get_bus(), proxy->watch_id);

	proxy_free(proxy);
}

int proxy_start(void)
{
	device_list = l_hashmap_new();

	hal_log_info("D-Bus Proxy");

	/*
	 * TODO: Add API to allow registering proxies dynamically.
	 * nrfd, iwpand or any other radio should implement a well
	 * defined interface to report new devices found or created.
	 */
	proxy = watch_create("br.org.cesar.knot", NULL, "br.org.cesar.knot.Device1");

	return device_start();
}

void proxy_stop(void)
{
	l_hashmap_destroy(device_list,
			  (l_hashmap_destroy_func_t) device_destroy);

	watch_remove(proxy);

	device_stop();
}
