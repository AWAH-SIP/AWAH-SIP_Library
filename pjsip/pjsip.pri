# QT Include File for PJSIP Library for AWAHSipLib

macx {
    message("include PJSIP Library for MacOS")
    include(mac_x86-64/pjsip_mac_x86-64_osx_aac.pri)
}

linux-g++ {
    message("includes PJSIP Library for linux x86-64")
    include(linux_x86-64/pjsip_linux_x86-64.pri)
}

win32 {
    message("includes PJSIP Library for Windows x86-64")
    include(win_x86-64/pjsip_win_x86-64.pri)
}

OTHER_FILES +=
