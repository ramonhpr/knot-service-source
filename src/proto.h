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

typedef struct {
	char *data;
	size_t size;
} json_raw_t;

/* Node operations */
struct proto_ops {
	const char *name;
	unsigned int source_id;
	int (*probe) (const char *host, unsigned int port);
	void (*remove) (void);

	/* Abstraction for connect & close/sign-off */
	int (*connect) (void);
	void (*close) (int sock);

	/* Abstraction for session establishment or registration */
	int (*mknode) (int sock, const char *owner_uuid, json_raw_t *json);
	int (*signin) (int sock, const char *uuid, const char *token,
							json_raw_t *json);
	int (*rmnode)(int sock, const char *uuid, const char *token,
							json_raw_t *jbuf);
	/* Abstraction for device data */
	int (*schema) (int sock, const char *uuid, const char *token,
					const char *jreq, json_raw_t *json);
	int (*data) (int sock, const char *uuid, const char *token,
					const char *jreq, json_raw_t *jbuf);
	int (*fetch) (int sock, const char *uuid, const char *token,
							json_raw_t *json);
	int (*setdata) (int sock, const char *uuid, const char *token,
					const char *jreq, json_raw_t *json);
	/*
	 * Watch that polls or monitors the cloud to check if "CONFIG" changed
	 * or "SET DATA" or "GET DATA".
	 */
	unsigned int (*async) (int sock, const char *uuid, const char *token,
		void (*proto_watch_cb) (json_raw_t, void *), void *user_data,
		void (*proto_watch_destroy_cb) (void *));
	void (*async_stop) (int sock, unsigned int watch_id);
};

int proto_start(const struct settings *settings, struct proto_ops **proto_ops);
void proto_stop(void);
