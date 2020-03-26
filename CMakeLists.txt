cmake_minimum_required(VERSION 3.7)
project(thermal_seek_xr_image_streamer)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package( OpenCV REQUIRED )

link_directories(
        /home/atomicbomber/CLionProjects/libseek-thermal/build/src
)

link_libraries(
        seek_static
        boost_program_options
        boost_system
        boost_filesystem
        ${OpenCV_LIBRARIES}
        -lusb-1.0
        sfml-graphics
        sfml-window
        sfml-system
        sfml-network
)

add_executable(thermal_seek_xr_image_streamer main.cpp args.h)

include_directories(
        /home/atomicbomber/CLionProjects/libseek-thermal/src
        ${OpenCV_INCLUDE_DIRS}
        /usr/include/libusb-1.0
)

