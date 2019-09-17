#!/bin/bash
# This script is for conveniently building the dependancies for quic.
ROOT_DIR=$(pwd)

cores=4
if [ "$(uname)" == "Darwin" ]; then
  cores=$(sysctl -n hw.ncpu)
elif [ "$(uname)" == "FreeBSD" ]; then
  cores=$(sysctl -n hw.ncpu)
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
  cores=$(nproc --all)
fi

echo "building quicly"
if cd quicly; then
  git pull
  git submodule update --recursive
  cd build
else
  git clone https://github.com/h2o/quicly.git
  cd quicly
  git submodule update --init --recursive
  mkdir build
  cd build
fi
cmake ..
make -j$cores
cd ..

echo "building picotls"
cd deps/picotls
if !(cd build); then
  mkdir build
  cd build
  cmake ..
  make -j$cores
fi
cd $ROOT_DIR  
