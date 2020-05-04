FROM ubuntu:20.04

RUN apt update -y && \
  apt upgrade -y

RUN apt install -y \
  cmake \
  g++-9 \
  git \
  libssl-dev \
  make \
 && apt-get autoclean

ENV CXX=/usr/bin/g++-9
