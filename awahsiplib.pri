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
    $$PWD/log.cpp \
    $$PWD/messagemanager.cpp \
    $$PWD/pjaccount.cpp \
    $$PWD/pjbuddy.cpp \
    $$PWD/pjcall.cpp \
    $$PWD/pjlogwriter.cpp \
    $$PWD/settings.cpp \
    $$PWD/websocket.cpp

HEADERS += \
    $$PWD/accounts.h \
    $$PWD/audiorouter.h \
    $$PWD/awahsiplib.h \
    $$PWD/buddies.h \
    $$PWD/codecs.h \
    $$PWD/log.h \
    $$PWD/messagemanager.h \
    $$PWD/pjaccount.h \
    $$PWD/pjbuddy.h \
    $$PWD/pjcall.h \
    $$PWD/pjlogwriter.h \
    $$PWD/settings.h \
    $$PWD/types.h \
    $$PWD/websocket.h

include(pjsip/pjsip.pri)
