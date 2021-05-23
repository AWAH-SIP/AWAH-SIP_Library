#-------------------------------------------------
#
#   AWAHSip Library QT Include File
#
#-------------------------------------------------

QT       += websockets

SOURCES += \
    $$PWD/accounts.cpp \
    $$PWD/audiorouter.cpp \
    $$PWD/awahsiplib.cpp \
    $$PWD/buddies.cpp \
    $$PWD/codecs.cpp \
    $$PWD/gpiodevice.cpp \
    $$PWD/gpiodevicemanager.cpp \
    $$PWD/gpiorouter.cpp \
    $$PWD/libgpiod_device.cpp \
    $$PWD/log.cpp \
    $$PWD/messagemanager.cpp \
    $$PWD/pjaccount.cpp \
    $$PWD/pjbuddy.cpp \
    $$PWD/pjcall.cpp \
    $$PWD/pjendpoint.cpp \
    $$PWD/pjlogwriter.cpp \
    $$PWD/settings.cpp \
    $$PWD/websocket.cpp

HEADERS += \
    $$PWD/accounts.h \
    $$PWD/audiorouter.h \
    $$PWD/awahsiplib.h \
    $$PWD/buddies.h \
    $$PWD/codecs.h \
    $$PWD/gpiodevice.h \
    $$PWD/gpiodevicemanager.h \
    $$PWD/gpiorouter.h \
    $$PWD/libgpiod_device.h \
    $$PWD/log.h \
    $$PWD/messagemanager.h \
    $$PWD/pjaccount.h \
    $$PWD/pjbuddy.h \
    $$PWD/pjcall.h \
    $$PWD/pjendpoint.h \
    $$PWD/pjlogwriter.h \
    $$PWD/settings.h \
    $$PWD/types.h \
    $$PWD/websocket.h

include(pjsip/pjsip.pri)

contains( DEFINES, AWAH_libgpiod ) {
    message("includes libgpiod Library so Linux Generic GPIO Device will be enabled")
    LIBS += -lgpiodcxx
    INCLUDEPATH += $$PWD/libgpiod/bindings/cxx/
}

OTHER_FILES += \
    $$PWD/libgpiod/bindings/cxx/*
