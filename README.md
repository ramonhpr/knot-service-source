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
- ell >= 0.4
- glib >= 2.56.1
- json-c v0.13.1
- automake
- libtool
- libwebsocket v2.4.2
- libssl-dev
- valgrind (optional)

## How to install dependencies:

`$ sudo apt-get install pkg-config autoconf automake libtool dbus libdbus-1-dev`

### Install glib
To install glib first you need to install the following packages:
`$ sudo apt-get install zlib1g-dev libffi-dev libmount-dev libpcre3-dev gettext python`

After install them, install the glib version >= 2.56.1
https://github.com/GNOME/glib/releases


### Install libwebsockets
To install libwebsockets first you need to install cmake (https://cmake.org/install/)

After install cmake, install libwebsocket v2.4.2
https://github.com/warmcat/libwebsockets/releases/tag/v2.4.2

### Install libell
To install libell, you have to clone the below repository and follow the instructions to install it:

git://git.kernel.org/pub/scm/libs/ell/ell.git

### Install json-c

To install the version 0.13.1 of json-c, you have to clone the repository and follow the instructions:
https://github.com/json-c/json-c/releases/tag/json-c-0.13.1-20180305

## How to build:
You have to install the knot-protocol-source and the knot-hal-source, so you can run:

`$./bootstrap-configure && make`

### How to check for memory leaks and open file descriptors:
```shell
$ valgrind --leak-check=full --track-fds=yes ./src/knotd \
--config=src/knotd.conf --proto=ws
```

### How to test (Unix sockets):

`$src/knotd`
`$tools/ktool connect`

### How to run 'knotd' specifying host & port:

`$src/knotd --config=./src/knotd.conf --proto=ws --host=localhost --port=3000`
