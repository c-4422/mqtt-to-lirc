#!/bin/bash

# Run this scripts to download and build static libraries needed for the program
###############################
# IMPORTANT THIS NEEDS TO BE ADDED TO CMakeLists in:
# paho.mqtt.cpp/src/CMakeLists.txt
# ${PROJECT_SOURCE_DIR}/../paho.mqtt.c/src/libpaho-mqtt3as.a
###############################

if [ ! -d "libsndfile" ]; then
    git clone https://github.com/libsndfile/libsndfile.git libsndfile
fi
if [ ! -d "paho.mqtt.c" ]; then
    git clone https://github.com/eclipse/paho.mqtt.c.git paho.mqtt.c
fi
if [ ! -d "paho.mqtt.cpp" ]; then
    git clone https://github.com/eclipse/paho.mqtt.cpp.git paho.mqtt.cpp
fi

cd paho.mqtt.c
cmake -Bbuild -H. -DPAHO_ENABLE_TESTING=OFF -DPAHO_BUILD_STATIC=ON \
    -DPAHO_WITH_SSL=ON -DPAHO_HIGH_PERFORMANCE=ON
sudo cmake --build build/ --target install
sudo ldconfig
cmake -DPAHO_BUILD_STATIC=TRUE -DPAHO_WITH_SSL=TRUE && cmake --build .
cd ../paho.mqtt.cpp
cmake -DPAHO_BUILD_STATIC=ON && cmake --build .
cd ../libsndfile
cmake -DENABLE_EXTERNAL_LIBS=FALSE -DENABLE_MPEG=OFF && cmake --build .

echo "The following packages are also required:"
echo "- lirc"
echo "- libao"