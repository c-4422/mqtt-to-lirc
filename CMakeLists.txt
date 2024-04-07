cmake_minimum_required(VERSION 3.10)
project(remote VERSION 1.0)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/paho.mqtt.cpp/cmake")

set(CMAKE_CXX_STANDARD 17)

set(PAHO_BUILD_STATIC TRUE)
set(PAHO_BUILD_SHARED FALSE)
set(PAHO_WITH_SSL TRUE)
set(PAHO_ENABLE_TESTING FALSE)
set(PAHO_MQTT_C_LIBRARIES "${PROJECT_SOURCE_DIR}/paho.mqtt.c/src")
set(PAHO_MQTT_C_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/paho.mqtt.c/src")

include_directories("paho.mqtt.c/src" "paho.mqtt.cpp/src")

link_directories(   "${PROJECT_SOURCE_DIR}/build/paho.mqtt.cpp/src"
                    "${PROJECT_SOURCE_DIR}/build/paho.mqtt.c/src"
                    "${PROJECT_SOURCE_DIR}/build/paho.mqtt.c/src/CMakeFiles/*"
                    "${PROJECT_SOURCE_DIR}/build/paho.mqtt.cpp/src/CMakeFiles/*")


add_subdirectory(paho.mqtt.c)
add_subdirectory(paho.mqtt.cpp)

find_package(Threads)
find_package(PahoMqttC)
find_package(OpenSSL)

add_executable(remote remote.cpp)
target_link_libraries( remote 
        ${CMAKE_THREAD_LIBS_INIT}
        ${PROJECT_SOURCE_DIR}/paho.mqtt.cpp/src/libpaho-mqttpp3.a
        ${PROJECT_SOURCE_DIR}/paho.mqtt.c/src/libpaho-mqtt3as.a
        ao
        ${PROJECT_SOURCE_DIR}/libsndfile/libsndfile.a
        lirc_client
        ${OPENSSL_SSL_LIBRARIES}
        ${OPENSSL_CRYPTO_LIBRARIES}
        ${CMAKE_DL_LIBS}
        )
