FROM frolvlad/alpine-gcc:latest AS builder

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
RUN apk update && apk add --no-cache \
       wget \
       pkgconfig autoconf automake libtool dbus dbus-dev

WORKDIR /usr/local
# install glib dependency
RUN apk add --no-cache \
      glib-dev
       #zlib-dev musl libintl gettext-dev libffi-dev libmount pcre-dev python

# RUN mkdir -p /usr/local/glib
# RUN wget -q --progress=bar -O- https://github.com/GNOME/glib/archive/$GLIB_VERSION.tar.gz| tar xz -C /usr/local/glib --strip-components=1
# RUN cd glib && ./autogen.sh && ./configure --disable-dependency-tracking -q && make install
RUN apk add --no-cache file make
# install json-c dependency
RUN mkdir -p /usr/local/jsonc
RUN wget -q -O- https://github.com/json-c/json-c/archive/json-c-$JSONC_VERSION.tar.gz | tar xz -C /usr/local/jsonc --strip-components=1
RUN cd jsonc && ./configure --prefix=/usr -q && make install

# install libell
RUN mkdir -p /usr/local/ell
RUN wget -q -O- https://mirrors.edge.kernel.org/pub/linux/libs/ell/ell-$LIBELL_VERSION.tar.gz|tar xz -C /usr/local/ell --strip-components=1
RUN cd ell && ./configure --prefix=/usr && make install

#install librabbitmq-c
RUN apk add --no-cache \
      cmake openssl-dev
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
# RUN mkdir -p /etc/dbus-1/system.d/

# generate Makefile
RUN PKG_CONFIG_PATH=/usr/lib64/pkgconfig ./bootstrap-configure

# build
RUN make install

FROM alpine:latest

ENV RABBITMQ_HOSTNAME rabbitmq
ENV RABBITMQ_PORT 5672

RUN apk add --no-cache dbus

WORKDIR /usr/local

COPY --from=builder /usr/lib/ /usr/lib/
COPY --from=builder /usr/lib64/ /usr/lib/
COPY --from=builder /etc/dbus-1/system.d/ /etc/dbus-1/system.d/
COPY --from=builder /usr/local/src/knotd /usr/bin/knotd
COPY --from=builder /usr/local/inetbr/inetbrd /usr/bin/inetbrd
COPY --from=builder /usr/local/docker/knot-service ./docker/knot-service

CMD ["./docker/knot-service"]
