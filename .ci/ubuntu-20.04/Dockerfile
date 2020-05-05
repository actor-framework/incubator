FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && \
  apt-get upgrade -y

RUN apt-get install -y \
  cmake \
  g++-9 \
  git \
  libssl-dev \
  make \
 && apt-get autoclean

ENV CXX=/usr/bin/g++-9
