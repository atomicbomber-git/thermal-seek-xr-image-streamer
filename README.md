# Setup

## TL;DR Quick Install:
```bash
# Run inside the project directory
chmod +x install.sh
./install.sh
```

## How to Use
```text
  ./streamer {OPTIONS}

    Seek Thermal Data Streamer

  OPTIONS:

      -h, --help                        Display this help menu
      --host=[arg_target_host]          Target host
      --port=[arg_target_port]          Target port
      --preadd=[arg_preadd]             Pre-Addition Temp Shift
      --postadd=[arg_postadd]           Post-Addition Temp Shift
      --multiplier=[arg_multiplier]     Multiplier for Temp
```

## Dependencies for Compiling
- libusb-1.0-0-dev
- libboost-program-options-dev
- libboost-system-dev
- libboost-filesystem-dev
- libsfml-dev
- libopencv-dev

## Dependencies for Compiling 
- build-essential
- cmake
- git@github.com:atomicbomber-git/libseek-thermal.git

Modify this project's CmakeLists.txt accordingly to point to the headers in `libseek-thermal`'s directory.

## The Command to Install All of Them
```bash

sudo apt-get install build-essential cmake libusb-1.0-0-dev libboost-program-options-dev libboost-system-dev libboost-filesystem-dev libsfml-dev libopencv-dev

git clone git@github.com:atomicbomber-git/libseek-thermal.git
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