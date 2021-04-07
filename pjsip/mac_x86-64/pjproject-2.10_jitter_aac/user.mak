# You can create user.mak file in PJ root directory to specify
# additional flags to compiler and linker. For example:
export CFLAGS += -I/Users/andy/Dropbox/ElektronikundSoft/Projects/AWAH_SIP/AWAH_test/awah-sip-testapp/lib/pjsip/mac_x86-64/fdk-aac-master/libAACdec/include \
        -I/Users/andy/Dropbox/ElektronikundSoft/Projects/AWAH_SIP/AWAH_test/awah-sip-testapp/lib/pjsip/mac_x86-64/fdk-aac-master/libAACenc/include \
        -I/Users/andy/Dropbox/ElektronikundSoft/Projects/AWAH_SIP/AWAH_test/awah-sip-testapp/lib/pjsip/mac_x86-64/fdk-aac-master/libPCMutils/include \
        -I/Users/andy/Dropbox/ElektronikundSoft/Projects/AWAH_SIP/AWAH_test/awah-sip-testapp/lib/pjsip/mac_x86-64/fdk-aac-master/libFDK/include \
        -I/Users/andy/Dropbox/ElektronikundSoft/Projects/AWAH_SIP/AWAH_test/awah-sip-testapp/lib/pjsip/mac_x86-64/fdk-aac-master/libSYS/include \
        -I/Users/andy/Dropbox/ElektronikundSoft/Projects/AWAH_SIP/AWAH_test/awah-sip-testapp/lib/pjsip/mac_x86-64/fdk-aac-master/libMpegTPDec/include \
        -I/Users/andy/Dropbox/ElektronikundSoft/Projects/AWAH_SIP/AWAH_test/awah-sip-testapp/lib/pjsip/mac_x86-64/fdk-aac-master/libMpegTPEnc/include \
        -I/Users/andy/Dropbox/ElektronikundSoft/Projects/AWAH_SIP/AWAH_test/awah-sip-testapp/lib/pjsip/mac_x86-64/fdk-aac-master/libSBRdec/include \
        -I/Users/andy/Dropbox/ElektronikundSoft/Projects/AWAH_SIP/AWAH_test/awah-sip-testapp/lib/pjsip/mac_x86-64/fdk-aac-master/libSBRenc/include
export LDFLAGS += -L/Users/andy/Dropbox/ElektronikundSoft/Projects/AWAH_SIP/AWAH_test/awah-sip-testapp/lib/pjsip/mac_x86-64/fdk-aac-master/build \
		-lfdk-aac

