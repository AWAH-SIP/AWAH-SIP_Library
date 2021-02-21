# PJSIP include for Windows x86-64

#    DEFINES     +=  PJ_WIN32=1 \
#                    PJMEDIA_HAS_OPUS_CODEC=1
#
    QMAKE_LFLAGS  += " /NODEFAULTLIB:LIBCMT "
    Debug:QMAKE_LFLAGS  += " /NODEFAULTLIB:LIBCMTD "

    INCLUDEPATH += $$PWD/pjproject-2.10/pjsip/include \
            $$PWD/pjproject-2.10/pjlib/include \
            $$PWD/pjproject-2.10/pjlib-util/include \
            $$PWD/pjproject-2.10/pjmedia/include \
            $$PWD/pjproject-2.10/pjnath/include \
            $$PWD/pjproject-2.10/include \
            $$PWD/opus-1.3.1/include

    Release:LIBS        += \
                -L$$PWD/pjproject-2.10/lib \
                -L$$PWD/pjproject-2.10/pjlib/lib \
                -L$$PWD/pjproject-2.10/pjlib-util/lib \
                -L$$PWD/pjproject-2.10/pjnath/lib \
                -L$$PWD/pjproject-2.10/pjmedia/lib \
                -L$$PWD/pjproject-2.10/pjsip/lib \
                -L$$PWD/pjproject-2.10/third_party/lib \
                -L$$PWD/opus-1.3.1/win32/VS2015/x64/Release \
                -llibpjproject-x86_64-x64-vc14-Release \
                -lpjsua2-lib-x86_64-x64-vc14-Release \
                -lpjsua-lib-x86_64-x64-vc14-Release \
                -lpjsip-ua-x86_64-x64-vc14-Release \
                -lpjsip-simple-x86_64-x64-vc14-Release \
                -lpjsip-core-x86_64-x64-vc14-Release \
                -lpjmedia-codec-x86_64-x64-vc14-Release \
                -lpjmedia-x86_64-x64-vc14-Release \
                -lpjmedia-videodev-x86_64-x64-vc14-Release \
                -lpjmedia-audiodev-x86_64-x64-vc14-Release \
                -lpjmedia-x86_64-x64-vc14-Release \
                -lpjnath-x86_64-x64-vc14-Release \
                -lpjlib-util-x86_64-x64-vc14-Release \
                -llibsrtp-x86_64-x64-vc14-Release \
                -llibresample-x86_64-x64-vc14-Release \
                -llibgsmcodec-x86_64-x64-vc14-Release \
                -llibspeex-x86_64-x64-vc14-Release \
                -llibilbccodec-x86_64-x64-vc14-Release \
                -llibg7221codec-x86_64-x64-vc14-Release \
                -llibyuv-x86_64-x64-vc14-Release \
                -llibwebrtc-x86_64-x64-vc14-Release \
                -lpjlib-x86_64-x64-vc14-Release \
                -lopus \
                -lwinmm \
                -lole32 \
                -lws2_32 \
                -lwsock32 \
                -lgdi32

    Debug:LIBS        += \
            -L$$PWD/pjproject-2.10/lib \
            -L$$PWD/pjproject-2.10/pjlib/lib \
            -L$$PWD/pjproject-2.10/pjlib-util/lib \
            -L$$PWD/pjproject-2.10/pjnath/lib \
            -L$$PWD/pjproject-2.10/pjmedia/lib \
            -L$$PWD/pjproject-2.10/pjsip/lib \
            -L$$PWD/pjproject-2.10/third_party/lib \
            -L$$PWD/opus-1.3.1/win32/VS2015/x64/Release \
            -llibpjproject-x86_64-x64-vc14-Debug-Dynamic \
            -lpjsua2-lib-x86_64-x64-vc14-Debug-Dynamic \
            -lpjsua-lib-x86_64-x64-vc14-Debug-Dynamic \
            -lpjsip-ua-x86_64-x64-vc14-Debug-Dynamic \
            -lpjsip-simple-x86_64-x64-vc14-Debug-Dynamic \
            -lpjsip-core-x86_64-x64-vc14-Debug-Dynamic \
            -lpjmedia-codec-x86_64-x64-vc14-Debug-Dynamic \
            -lpjmedia-x86_64-x64-vc14-Debug-Dynamic \
            -lpjmedia-videodev-x86_64-x64-vc14-Debug-Dynamic \
            -lpjmedia-audiodev-x86_64-x64-vc14-Debug-Dynamic \
            -lpjmedia-x86_64-x64-vc14-Debug-Dynamic \
            -lpjnath-x86_64-x64-vc14-Debug-Dynamic \
            -lpjlib-util-x86_64-x64-vc14-Debug-Dynamic \
            -llibsrtp-x86_64-x64-vc14-Debug-Dynamic \
            -llibresample-x86_64-x64-vc14-Debug-Dynamic \
            -llibgsmcodec-x86_64-x64-vc14-Debug-Dynamic \
            -llibspeex-x86_64-x64-vc14-Debug-Dynamic \
            -llibilbccodec-x86_64-x64-vc14-Debug-Dynamic \
            -llibg7221codec-x86_64-x64-vc14-Debug-Dynamic \
            -llibyuv-x86_64-x64-vc14-Debug-Dynamic \
            -llibwebrtc-x86_64-x64-vc14-Debug-Dynamic \
            -lpjlib-x86_64-x64-vc14-Debug-Dynamic \
            -lopus \
            -lwinmm \
            -lole32 \
            -lws2_32 \
            -lwsock32 \
            -lgdi32
