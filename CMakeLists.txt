cmake_minimum_required(VERSION 3.16)
project(AWAH_SIP_Library LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_INCLUDE_CURRENT_DIR OFF)

find_package(QT NAMES Qt6 COMPONENTS Core WebSockets REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core WebSockets REQUIRED)

add_library(AWAH_SIP_Library STATIC
            include/accounts.h 
            include/audiorouter.h 
            include/awahsiplib.h 
            include/buddies.h 
            include/codecs.h 
            include/gpiodevice.h 
            include/gpiodevicemanager.h 
            include/gpiorouter.h 
            include/libgpiod_device.h 
            include/log.h 
            include/messagemanager.h 
            include/pjaccount.h 
            include/pjbuddy.h 
            include/pjcall.h 
            include/pjendpoint.h 
            include/pjlogwriter.h 
            include/settings.h 
            include/types.h 
            include/websocket.h 
            include/webrtcchannels.h
            include/latencymon_port.h
            include/LatencyMonitorDevice.h

            src/accounts.cpp 
            src/audiorouter.cpp 
            src/awahsiplib.cpp 
            src/buddies.cpp 
            src/codecs.cpp 
            src/gpiodevice.cpp 
            src/gpiodevicemanager.cpp 
            src/gpiorouter.cpp 
            src/libgpiod_device.cpp 
            src/log.cpp 
            src/messagemanager.cpp 
            src/pjaccount.cpp 
            src/pjbuddy.cpp 
            src/pjcall.cpp 
            src/pjendpoint.cpp 
            src/pjlogwriter.cpp 
            src/settings.cpp 
            src/websocket.cpp 
            src/webrtcchannels.cpp
            src/latencymon_port.cpp
            src/LatencyMonitorDevice.cpp
)

include_directories(${PKG_PJSIP_INCLUDE_DIRS})
target_link_libraries(AWAH_SIP_Library PUBLIC PkgConfig::PKG_PJSIP PkgConfig::PKG_OPUS Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::WebSockets)

target_compile_definitions(AWAH_SIP_Library PRIVATE AWAH_SIP_Library_LIBRARY)


