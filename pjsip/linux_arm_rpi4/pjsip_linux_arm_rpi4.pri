# PJSIP include for Linux arm

DEFINES += PJ_IS_LITTLE_ENDIAN=1 \
           PJ_IS_BIG_ENDIAN=0

INCLUDEPATH += $$PWD/pjproject-2.10/pjsip/include \
            $$PWD/pjproject-2.10/pjlib/include \
            $$PWD/pjproject-2.10/pjlib-util/include \
            $$PWD/pjproject-2.10/pjmedia/include \
            $$PWD/pjproject-2.10/pjnath/include

    LIBS += \
            -L$$PWD/pjproject-2.10/pjlib/lib \
            -L$$PWD/pjproject-2.10/pjlib-util/lib \
            -L$$PWD/pjproject-2.10/pjnath/lib \
            -L$$PWD/pjproject-2.10/pjmedia/lib \
            -L$$PWD/pjproject-2.10/pjsip/lib \
            -L$$PWD/pjproject-2.10/third_party/lib            \
            -lpjsua2-armv7l-unknown-linux-gnueabihf \
            -lstdc++ \
            -lpjsua-armv7l-unknown-linux-gnueabihf \
            -lpjsip-ua-armv7l-unknown-linux-gnueabihf \
            -lpjsip-simple-armv7l-unknown-linux-gnueabihf \
            -lpjsip-armv7l-unknown-linux-gnueabihf \
            -lpjmedia-codec-armv7l-unknown-linux-gnueabihf \
            -lpjmedia-armv7l-unknown-linux-gnueabihf \
            -lpjmedia-videodev-armv7l-unknown-linux-gnueabihf \
            -lpjmedia-audiodev-armv7l-unknown-linux-gnueabihf \
            -lpjmedia-armv7l-unknown-linux-gnueabihf \
            -lpjnath-armv7l-unknown-linux-gnueabihf \
            -lpjlib-util-armv7l-unknown-linux-gnueabihf  \
            -lsrtp-armv7l-unknown-linux-gnueabihf \
            -lresample-armv7l-unknown-linux-gnueabihf \
            -lgsmcodec-armv7l-unknown-linux-gnueabihf \
            -lspeex-armv7l-unknown-linux-gnueabihf \
            -lilbccodec-armv7l-unknown-linux-gnueabihf \
            -lg7221codec-armv7l-unknown-linux-gnueabihf \
            -lyuv-armv7l-unknown-linux-gnueabihf \
            -lwebrtc-armv7l-unknown-linux-gnueabihf  \
            -lpj-armv7l-unknown-linux-gnueabihf \
            -lopus \
            -lssl \
            -lcrypto \
            -lm \
            -lrt \
            -lpthread  \
            -lasound
