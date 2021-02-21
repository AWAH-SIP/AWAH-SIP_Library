# PJSIP include for MacOS x86-64

INCLUDEPATH += $$PWD/pjproject-2.10_osx_aac/pjsip/include \
            $$PWD/pjproject-2.10_osx_aac/pjlib/include \
            $$PWD/pjproject-2.10_osx_aac/pjlib-util/include \
            $$PWD/pjproject-2.10_osx_aac/pjmedia/include \
            $$PWD/pjproject-2.10_osx_aac/pjnath/include

    LIBS += \
            -L$$PWD/pjproject-2.10_osx_aac/pjlib/lib \
            -L$$PWD/pjproject-2.10_osx_aac/pjlib-util/lib \
            -L$$PWD/pjproject-2.10_osx_aac/pjnath/lib \
            -L$$PWD/pjproject-2.10_osx_aac/pjmedia/lib \
            -L$$PWD/pjproject-2.10_osx_aac/pjsip/lib \
            -L$$PWD/pjproject-2.10_osx_aac/third_party/lib \
            -L$$PWD/fdk-aac-master/build \
            -L/usr/local/Cellar/opus/1.3.1/lib \
            -lpjsua2-x86_64-apple-darwin20.3.0 \
            -lstdc++ \
            -lpjsua-x86_64-apple-darwin20.3.0 \
            -lpjsip-ua-x86_64-apple-darwin20.3.0 \
            -lpjsip-simple-x86_64-apple-darwin20.3.0 \
            -lpjsip-x86_64-apple-darwin20.3.0 \
            -lpjmedia-codec-x86_64-apple-darwin20.3.0 \
            -lpjmedia-x86_64-apple-darwin20.3.0 \
            -lpjmedia-videodev-x86_64-apple-darwin20.3.0 \
            -lpjmedia-audiodev-x86_64-apple-darwin20.3.0 \
            -lpjmedia-x86_64-apple-darwin20.3.0 \
            -lpjnath-x86_64-apple-darwin20.3.0 \
            -lpjlib-util-x86_64-apple-darwin20.3.0 \
            -lsrtp-x86_64-apple-darwin20.3.0 \
            -lresample-x86_64-apple-darwin20.3.0 \
            -lgsmcodec-x86_64-apple-darwin20.3.0 \
            -lspeex-x86_64-apple-darwin20.3.0 \
            -lilbccodec-x86_64-apple-darwin20.3.0 \
            -lg7221codec-x86_64-apple-darwin20.3.0 \
            -lyuv-x86_64-apple-darwin20.3.0 \
            -lwebrtc-x86_64-apple-darwin20.3.0 \
            -lpj-x86_64-apple-darwin20.3.0 \
            -lm \
            -lpthread \
            -lopus \
            -lfdk-aac \
            -framework CoreAudio \
            -framework CoreServices \
            -framework AudioUnit \
            -framework AudioToolbox \
            -framework Foundation \
            -framework AppKit \
            -framework AVFoundation \
            -framework CoreGraphics \
            -framework QuartzCore \
            -framework CoreVideo \
            -framework CoreMedia \
            -framework VideoToolbox \
            -framework Security
