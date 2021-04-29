# PJSIP include for Linux x86-64

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
            -lpjsua2-x86_64-unknown-linux-gnu \
            -lstdc++ \
            -lpjsua-x86_64-unknown-linux-gnu \
            -lpjsip-ua-x86_64-unknown-linux-gnu \
            -lpjsip-simple-x86_64-unknown-linux-gnu \
            -lpjsip-x86_64-unknown-linux-gnu \
            -lpjmedia-codec-x86_64-unknown-linux-gnu \
            -lpjmedia-x86_64-unknown-linux-gnu \
            -lpjmedia-videodev-x86_64-unknown-linux-gnu \
            -lpjmedia-audiodev-x86_64-unknown-linux-gnu \
            -lpjmedia-x86_64-unknown-linux-gnu \
            -lpjnath-x86_64-unknown-linux-gnu \
            -lpjlib-util-x86_64-unknown-linux-gnu  \
            -lsrtp-x86_64-unknown-linux-gnu \
            -lresample-x86_64-unknown-linux-gnu \
            -lgsmcodec-x86_64-unknown-linux-gnu \
            -lspeex-x86_64-unknown-linux-gnu \
            -lilbccodec-x86_64-unknown-linux-gnu \
            -lg7221codec-x86_64-unknown-linux-gnu \
            -lyuv-x86_64-unknown-linux-gnu \
            -lwebrtc-x86_64-unknown-linux-gnu  \
            -lpj-x86_64-unknown-linux-gnu \
            -lopus \
            -lssl \
            -lcrypto \
            -luuid \
            -lm \
            -lrt \
            -lpthread  \
            -lasound


