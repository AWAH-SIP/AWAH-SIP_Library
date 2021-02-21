# You can create user.mak file in PJ root directory to specify
# additional flags to compiler and linker. For example:
export CFLAGS += -I/Users/adi/Documents/GitHub/awah-sip-testapp/lib/pjsip/fdk-aac-master/libAACdec/include \
        -I/Users/adi/Documents/GitHub/awah-sip-testapp/lib/pjsip/fdk-aac-master/libAACenc/include \
        -I/Users/adi/Documents/GitHub/awah-sip-testapp/lib/pjsip/fdk-aac-master/libPCMutils/include \
        -I/Users/adi/Documents/GitHub/awah-sip-testapp/lib/pjsip/fdk-aac-master/libFDK/include \
        -I/Users/adi/Documents/GitHub/awah-sip-testapp/lib/pjsip/fdk-aac-master/libSYS/include \
        -I/Users/adi/Documents/GitHub/awah-sip-testapp/lib/pjsip/fdk-aac-master/libMpegTPDec/include \
        -I/Users/adi/Documents/GitHub/awah-sip-testapp/lib/pjsip/fdk-aac-master/libMpegTPEnc/include \
        -I/Users/adi/Documents/GitHub/awah-sip-testapp/lib/pjsip/fdk-aac-master/libSBRdec/include \
        -I/Users/adi/Documents/GitHub/awah-sip-testapp/lib/pjsip/fdk-aac-master/libSBRenc/include
export LDFLAGS += -lfdk-aac

