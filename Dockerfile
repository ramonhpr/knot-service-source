FROM solita/ubuntu-systemd:latest

# build arguments
ARG GLIB_VERSION=2.56.1
ARG JSONC_VERSION=0.13.1-20180305
ARG LIBELL_VERSION=0.17
ARG RABBITMQC_VERSION=v0.9.0
ARG KNOT_PROTOCOL_VERSION=KNOT-v02.01-rc01
ARG KNOT_HAL_VERSION=KNOT-v02.01-rc01

ENV RABBITMQ_HOSTNAME rabbitmq
ENV RABBITMQ_PORT 5672

# install dependencies
RUN apt-get update \
 && apt-get install -y \
       wget apt-transport-https \
       pkg-config autoconf automake libtool

RUN apt-get install -y \
      dbus libdbus-1-dev docker.io

WORKDIR /usr/local
# install glib dependency
RUN apt-get update \
 && apt-get install -y \
       zlib1g-dev libffi-dev libmount-dev libpcre3-dev gettext python

RUN mkdir -p /usr/local/glib
RUN wget -q --progress=bar -O- https://github.com/GNOME/glib/archive/$GLIB_VERSION.tar.gz| tar xz -C /usr/local/glib --strip-components=1
RUN cd glib && ./autogen.sh && ./configure -q && make install

# install json-c dependency
RUN mkdir -p /usr/local/jsonc
RUN wget -q -O- https://github.com/json-c/json-c/archive/json-c-$JSONC_VERSION.tar.gz | tar xz -C /usr/local/jsonc --strip-components=1
RUN cd jsonc && ./configure -q && make install

# install libell
RUN mkdir -p /usr/local/ell
RUN wget -q -O- https://mirrors.edge.kernel.org/pub/linux/libs/ell/ell-$LIBELL_VERSION.tar.gz|tar xz -C /usr/local/ell --strip-components=1
RUN cd ell && ./configure --prefix=/usr && make install

#install librabbitmq-c
RUN apt-get install -y \
      cmake libssl-dev
RUN mkdir -p /usr/local/rabbitmq-c
RUN wget -q -O- https://github.com/alanxz/rabbitmq-c/archive/$RABBITMQC_VERSION.tar.gz|tar xz -C /usr/local/rabbitmq-c --strip-components=1
RUN cd rabbitmq-c && mkdir build && cd build && cmake -DCMAKE_INSTALL_PREFIX=/usr .. && make install

# install knot-protocol
RUN mkdir -p /usr/local/protocol
RUN wget -q -O- https://github.com/CESARBR/knot-protocol-source/archive/$KNOT_PROTOCOL_VERSION.tar.gz|tar xz -C /usr/local/protocol --strip-components=1
RUN cd protocol && ./bootstrap-configure && make install

# install knot-hal
RUN mkdir -p /usr/local/hal
RUN wget -q -O- https://github.com/CESARBR/knot-hal-source/archive/$KNOT_HAL_VERSION.tar.gz|tar xz -C /usr/local/hal --strip-components=1
RUN cd hal && ./bootstrap-configure && make install

# copy files to source
COPY ./ ./
COPY ./docker/system.conf /usr/share/dbus-1/system.conf

# dbus conf files
COPY ./src/knot.conf /etc/dbus-1/system.d

# generate Makefile
RUN ./bootstrap-configure

# build
RUN make install

CMD ["./docker/knot-service"]
