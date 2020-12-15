# Setup

## Dependencies for Running
- libusb-1.0-0-dev
- libboost-program-options-dev
- libboost-system-dev
- libboost-filesystem-dev
- libsfml-dev
- libopencv-dev

## Dependencies for Compiling 
- build-essential
- cmake
- https://github.com/undera/libseek-thermal.git

For the above `libseek-thermal` repo, clone it, and run `git checkout temp-info` before compiling. Modify this project's CmakeLists.txt accordingly to point to the headers in `libseek-thermal`'s directory.

## The Command to Install All of Them
```bash

sudo apt-get install build-essential cmake libusb-1.0-0-dev libboost-program-options-dev libboost-system-dev libboost-filesystem-dev libsfml-dev libopencv-dev

git clone https://github.com/undera/libseek-thermal.git
git checkout temp-info
mkdir build
cd build
cmake ..
make

```
## The `udev` Rule

```bash
# Put this in this file:
# /etc/udev/rules.d/51-seek-thermal.rules

SUBSYSTEM=="usb", ATTR{idVendor}=="289d", ATTR{idProduct}=="0010", GROUP="plugdev"
```

```bash
# Reload udev rules 
sudo udevadm control --reload-rules
```

## REMEMBER TO `UNPLUG AND REPLUG THE DEVICE` AFTER THE UDEV RULE HAS BEEN MODIFIED `BEFORE RUNNING THE PROGRAM`