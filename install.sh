#!/bin/bash

LIBSEEK_PATH=../libseek-thermal
ABS_LIBSEEK_PATH=$(cd "$(dirname "$LIBSEEK_PATH")"; pwd -P)/$(basename "$LIBSEEK_PATH")
MAIN_WORKING_DIR=$(pwd)

sudo apt-get install build-essential cmake libusb-1.0-0-dev libboost-program-options-dev libboost-system-dev libboost-filesystem-dev libsfml-dev libopencv-dev
git clone https://github.com/atomicbomber-git/libseek-thermal.git $LIBSEEK_PATH

cd $LIBSEEK_PATH
mkdir build
cd build
cmake ..
make

cd $MAIN_WORKING_DIR
rm -rf build
mkdir build
cd build
export CXXFLAGS="-L$ABS_LIBSEEK_PATH/build/src -I$ABS_LIBSEEK_PATH/src"
cmake ..
make