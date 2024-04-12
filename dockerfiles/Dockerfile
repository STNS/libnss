FROM ubuntu:jammy
MAINTAINER pyama86 <www.kazu.com@gmail.com>
ENV LIBDIR /usr/lib
RUN apt update -qqy && \
    apt install -qqy sudo curl
RUN curl -fsSL https://repo.stns.jp/scripts/apt-repo.sh | sh
RUN apt install -qqy \
    build-essential  \
    make \
    git \
    vim \
    stns-v2 \
    cache-stnsd \
    systemd \
    init \
    netcat \
    libasan5 \
    valgrind \
    flawfinder
RUN mkdir /stns
WORKDIR /stns
