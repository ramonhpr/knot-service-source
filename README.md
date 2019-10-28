# KNOT Service for Linux systems

KNOT service is part of KNOT project. It aims to provide a "proxy" service
for KNOT nodes, allowing power constrained embedded device to interact
with Meshblu cloud services.

The initial target platform are nRF24L01 nodes, and Raspiberry PI GW. nRF24L01
is a highly integrated, ultra low power (ULP) 2Mbps RF transceiver IC for the
2.4GHz ISM band. On a second project phase, other radio access technologies
such as Bluetooth Low Energy, and Wi-Fi are planned.

Dependencies:
- knot-protocol-source
- knot-hal-source
- ell >= 1.7
- json-c v0.13.1
- rabbitmq-c
- automake
- libtool
- libssl-dev
- valgrind (optional)

## How to install dependencies:

`$ sudo apt-get install automake libtool libssl-dev valgrind`

### Install libell
To install libell, you have to clone the below repository and follow the instructions to install it:

git://git.kernel.org/pub/scm/libs/ell/ell.git

### Install json-c

To install the version 0.13.1 of json-c, you have to clone the repository and follow the instructions:
https://github.com/json-c/json-c/releases/tag/json-c-0.13.1-20180305

### Install rabbitmq-c

After install cmake, install rabbitmq-c. You have to clone the repository and follow the instructions:
https://github.com/alanxz/rabbitmq-c

## How to build:
You have to install the knot-protocol-source and the knot-hal-source, so you can run:

`$./bootstrap-configure && make`

### How to check for memory leaks and open file descriptors:
```shell
$ valgrind --leak-check=full --track-fds=yes ./src/knotd -nr
```

### How to test (Unix sockets):

`$src/knotd`
`$tools/ktool connect`

## How to run 'knotd' specifying host & port:

`$ sudo src/knotd -nr -c src/knotd.conf --rabbitmq-url amqp://user:password@serverdomain:5672/vhost`

## How to test a device connection:

The command bellow will register a device which sends a lamp status, and the
credentials in the path `./thing_credentials.json`:

`$ test/test-conn.py -f ./thing_credentials.json`

Note: If you run the same command again it will authenticate with knotd.

It's possible use options to change what the device can send as with the bellow command:

`$ test/test-conn.py -d json/data-array.json -s json/schema-array.json`
