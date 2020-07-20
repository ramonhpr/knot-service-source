#!/usr/bin/python
#
#  This file is part of the KNOT Project
#
#  Copyright (c) 2019, CESAR. All rights reserved.
#
#   This library is free software; you can redistribute it and/or
#   modify it under the terms of the GNU Lesser General Public
#   License as published by the Free Software Foundation; either
#   version 2.1 of the License, or (at your option) any later version.
#
#   This library is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   Lesser General Public License for more details.
#
#   You should have received a copy of the GNU Lesser General Public
#   License along with this library; if not, write to the Free Software
#   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

import socket
import struct
import json
import threading
import logging
import sys
import argparse
import knot_pb2 as knot
from time import time, sleep
from google.protobuf import service

parser = argparse.ArgumentParser(description='Simulates a thing connection')
parser.add_argument('-f', '--file', dest="filename",
                    help="write credentials to FILE", default="storage.json",
                    type=str, metavar="FILE")
parser.add_argument('-i', '--id', dest="id",
                    help="KNoT id", default=0x0123456789abcdef,
                    type=int, metavar="ID")
parser.add_argument('-n', '--name', dest="name",
                    help="thing name", default='Test',
                    type=str, metavar="NAME")
parser.add_argument('-s', '--schema', dest="schema_file",
                    help="schema file", default='',
                    type=str, metavar="SCHEMA_FILE")
parser.add_argument('-d', '--data', dest="data_file",
                    help="data file", default='',
                    type=str, metavar="DATA_FILE")
parser.add_argument('-H', '--host', dest="host",
                    help="host to connect", default='localhost',
                    type=str, metavar="HOST")
parser.add_argument('-p', '--port', dest="port",
                    help="port to connect", default='8884',
                    type=int, choices=range(0x10000),metavar="PORT")
parser.add_argument('--debug', action="store_true",
                    help="show debug messages")
options = parser.parse_args()

if options.debug:
    logging.basicConfig(level=logging.DEBUG, stream=sys.stderr)
else:
    logging.basicConfig(level=logging.INFO, stream=sys.stderr)

THING_ID = options.id
THING_NAME = options.name
HOST = options.host
PORT = options.port
if options.schema_file:
    with open(options.schema_file) as fd:
        schemas = json.load(fd)['schema']
    if options.data_file:
        with open(options.data_file) as fd:
            datas = json.load(fd)['data']
    else:
        raise argparse.ArgumentTypeError('Missing data file')
else:
    schemas = [{
        "sensor_id": 253, "value_type":3,
        "unit":0, "type_id": 65521,
        "name": "Lamp Status"
    }]
    datas = [{
        'sensor_id': 253,
        'value': False
    }]
schemas_sents = []

# definitions

def gen_register_msg(thing_id, name):
    msg = knot.knot_msg()
    msg.type = knot.knot_msg_type.REGISTER_REQ
    msg.payload_len = 8 + len(name)
    msg.reg_req.id = thing_id
    msg.reg_req.name = name
    return msg

class TcpChannel(service.RpcChannel):
    def __init__(self, host, port):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        logging.info('Connection to %s:%d', HOST, PORT)
        self.socket.connect((host, port))
        self.socket.settimeout(10)

    def CallMethod(self, method_descriptor, rpc_controller,
                 request, response_class, done):
        self.socket.send(request.SerializeToString())
        msg = None
        try:
            buffer = self.socket.recv(100)
            msg = response_class()
            msg.ParseFromString(buffer)
            logging.debug('[payload_len: %d] receive knot_msg=%s', len(buffer), msg)
        except socket.timeout:
            logging.error("operation %s timeout", method_descriptor)
        return msg

class KnotController(service.RpcController):
    pass

# Main code

try:
    with open(options.filename) as fd:
        credentials = json.load(fd)
except IOError:
    credentials = {}

channel = TcpChannel(HOST, PORT)
controller = KnotController()
knotService = knot.knot_sm_Stub(channel)

if not credentials:
    msg = gen_register_msg(THING_ID, THING_NAME)
    response = knotService.register_thing(controller, msg)
    while response.result != knot.SUCCESS:
        sleep(2)
        logging.debug("error! trying to send again")
        response = knotService.register_thing(controller, msg)
else:
    schemas_sents = schemas
    schemas = []
    nbytes = send_knot_msg_auth(s, str(credentials['uuid']), str(credentials['token']))
    if nbytes < 0:
        logging.debug(nbytes)

while True:
    pass