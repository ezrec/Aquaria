/*
 * Copyright (C) 2010, Jason S. McMullan. All rights reserved.
 * Author: Jason S. McMullan <jason.mcmullan@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef AQ_SERVER_H
#define AQ_SERVER_H

#include "aquaria.h"

struct aq_server_conn;

int aq_server_open(int port);
struct aq_server_conn *aq_server_connect(struct aquaria *aq, int listening_sock);
void aq_server_disconnect(struct aq_server_conn *conn);
int aq_server_handle(struct aq_server_conn *conn);
int aq_server_socket(struct aq_server_conn *conn);

#endif /* AQ_SERVER_H */
