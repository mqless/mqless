FROM ubuntu:bionic
MAINTAINER mqless Developers <somdorom@gmail.com>

RUN DEBIAN_FRONTEND=noninteractive apt-get update -y -q
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y -q --force-yes sudo build-essential git-core libtool autotools-dev autoconf automake pkg-config unzip libkrb5-dev cmake libnsspem libcurl4-nss-dev libmicrohttpd-dev libjansson-dev

WORKDIR /home/zmq/tmp-deps
RUN git clone --quiet https://github.com/zeromq/libzmq.git libzmq
WORKDIR /home/zmq/tmp-deps/libzmq
RUN ./autogen.sh 2> /dev/null
RUN ./configure --quiet --without-docs
RUN make
RUN sudo make install
RUN sudo ldconfig

WORKDIR /home/zmq/tmp-deps
RUN git clone --quiet https://github.com/zeromq/czmq.git czmq
WORKDIR /home/zmq/tmp-deps/czmq
RUN ./autogen.sh 2> /dev/null
RUN ./configure --quiet --without-docs
RUN make
RUN sudo make install
RUN sudo ldconfig

WORKDIR /home/zmq
COPY . mqless
# RUN git clone --quiet git://github.com/somdoron/mqless.git mqless
WORKDIR /home/zmq/mqless
RUN ./autogen.sh 2> /dev/null
RUN ./configure --quiet --without-docs
RUN make
RUN sudo make install
RUN sudo ldconfig

WORKDIR /home/zmq

EXPOSE 34543

ENTRYPOINT ["mqless"]