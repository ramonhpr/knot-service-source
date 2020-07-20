FROM alpine:3.10 AS builder

# Build arguments
ARG JSONC_VERSION=0.13.1-20180305
ARG LIBELL_VERSION=0.28
ARG RABBITMQC_VERSION=v0.9.0
ARG KNOT_PROTOCOL_VERSION=protobuf_v3
ARG KNOT_HAL_VERSION=KNOT-v02.01-rc01

# Install dependencies
RUN apk add --no-cache gcc musl-dev
RUN apk update && apk add --no-cache \
       wget \
       pkgconfig \
       autoconf \
       automake \
       libtool \
       dbus dbus-dev \
       glib-dev \
       file \
       g++ \
       make

WORKDIR /usr/local

# Install json-c dependency
RUN mkdir -p /usr/local/jsonc
RUN wget -q -O- https://github.com/json-c/json-c/archive/json-c-$JSONC_VERSION.tar.gz | tar xz -C /usr/local/jsonc --strip-components=1
RUN cd jsonc && ./configure --prefix=/usr -q && make install

# Install libell
RUN mkdir -p /usr/local/ell
RUN wget -q -O- https://mirrors.edge.kernel.org/pub/linux/libs/ell/ell-$LIBELL_VERSION.tar.gz|tar xz -C /usr/local/ell --strip-components=1
RUN cd ell && ./configure --prefix=/usr && make install

# Install librabbitmq-c
RUN apk add --no-cache \
      cmake openssl-dev
RUN mkdir -p /usr/local/rabbitmq-c
RUN wget -q -O- https://github.com/alanxz/rabbitmq-c/archive/$RABBITMQC_VERSION.tar.gz|tar xz -C /usr/local/rabbitmq-c --strip-components=1
RUN cd rabbitmq-c && mkdir build && cd build && cmake -DCMAKE_INSTALL_PREFIX=/usr .. && make install

# protobuf
RUN mkdir -p /usr/local/protobuf
RUN wget -q -O- https://github.com/protocolbuffers/protobuf/releases/download/v3.11.4/protobuf-all-3.11.4.tar.gz|tar xz -C /usr/local/protobuf --strip-components=1
RUN cd protobuf && ./autogen.sh && ./configure && make install

# libprotobuf-c
RUN mkdir -p /usr/local/protobuf-c
RUN wget -q -O- https://github.com/protobuf-c/protobuf-c/releases/download/v1.3.3/protobuf-c-1.3.3.tar.gz|tar xz -C /usr/local/protobuf-c --strip-components=1
RUN cd protobuf-c && ./configure && make install

# Install knot-protocol
RUN mkdir -p /usr/local/protocol
RUN wget -q -O- https://github.com/ramonhpr/knot-protocol-source/archive/$KNOT_PROTOCOL_VERSION.tar.gz|tar xz -C /usr/local/protocol --strip-components=1
RUN cd protocol && ./bootstrap-configure --enable-protobuf && make install

# Install knot-hal
RUN mkdir -p /usr/local/hal
RUN wget -q -O- https://github.com/CESARBR/knot-hal-source/archive/$KNOT_HAL_VERSION.tar.gz|tar xz -C /usr/local/hal --strip-components=1
RUN cd hal && ./bootstrap-configure && make install

# Copy files to source
COPY ./ ./

# Generate Makefile
RUN PKG_CONFIG_PATH=/usr/lib64/pkgconfig ./bootstrap-configure

# Build knotd and inetbrd
RUN make install

FROM alpine:3.10.3

ENV RABBITMQ_HOST rabbitmq
ENV RABBITMQ_PORT 5672

RUN apk add --no-cache dbus

WORKDIR /usr/local

# Copy shared files .so from builder to target image
COPY --from=builder /usr/lib/libell.so* /usr/lib/
COPY --from=builder /usr/lib/libjson-c.so* /usr/lib/
COPY --from=builder /usr/lib/libjson-c.a* /usr/lib/
COPY --from=builder /usr/lib/libknotprotocol.so* /usr/lib/
COPY --from=builder /usr/lib/libknotprotocol.a* /usr/lib/
COPY --from=builder /usr/local/lib/libprotobuf.so* /usr/lib/
COPY --from=builder /usr/local/lib/libprotobuf.a* /usr/lib/
COPY --from=builder /usr/local/lib/libprotobuf-c.so* /usr/lib/
COPY --from=builder /usr/local/lib/libprotobuf-c.a* /usr/lib/
COPY --from=builder /usr/lib/libhal.so* /usr/lib/
COPY --from=builder /usr/lib/libhal.a* /usr/lib/
COPY --from=builder /usr/lib64/librabbitmq.so* /usr/lib/
COPY --from=builder /usr/lib64/librabbitmq.a* /usr/lib/

# Copy dbus configuration
COPY --from=builder /etc/dbus-1/system.d/ /etc/dbus-1/system.d/

# Copy knotd configuration
COPY --from=builder /usr/local/src/knotd.conf /etc/knot/knotd.conf

# Copy binary executables
COPY --from=builder /usr/local/src/knotd /usr/bin/knotd
COPY --from=builder /usr/local/inetbr/inetbrd /usr/bin/inetbrd

# system confiuration to allow tcp over dbus
COPY ./docker/system.conf /usr/share/dbus-1/system.conf

CMD mkdir -p /var/run/dbus/ && dbus-daemon --system && sleep 2 && (inetbrd -n & knotd -nr -R amqp://$RABBITMQ_HOST:$RABBITMQ_PORT)
