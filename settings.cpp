/*
 * Copyright (C) 2016 - 2021 Andy Weiss, Adi Hilber
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "settings.h"
#include "awahsiplib.h"
#include "pjmedia.h"
#include "pjlib-util.h" /* pj_getopt */
#include "pjlib.h"
#include <QDebug>
#include <QThread>
#include <QSettings>
#include <QDir>

#define THIS_FILE		"settings.cpp"

Settings::Settings(AWAHSipLib *parentLib, QObject *parent) : QObject(parent), m_lib(parentLib)
{

}


void Settings::loadIODevConfig()
{
    //qRegisterMetaTypeStreamOperators <QList<s_IODevices>>("QList<s_IODevices>");
    QList<s_IODevices>  loadedDevices;
    int recordDevId, playbackDevId;
    QSettings settings("awah", "AWAHsipConfig");

    loadedDevices = settings.value("IODevConfig").value<QList<s_IODevices>>();

    for( int i=0; i<loadedDevices.count(); ++i )                                                     // todo send an error message if sound defice is not found!!
    {
        if(loadedDevices.at(i).devicetype == SoundDevice){
            recordDevId = m_lib->m_AudioRouter->getSoundDevID(loadedDevices.at(i).inputname);
            playbackDevId = m_lib->m_AudioRouter->getSoundDevID(loadedDevices.at(i).outputame);
            if (recordDevId != -1 && playbackDevId !=-1){
                m_lib->m_AudioRouter->addAudioDevice(recordDevId,playbackDevId, loadedDevices.at(i).uid);
                m_lib->m_Log->writeLog(3,QString("loadIODevConfig: added sound device from config file: ") + loadedDevices.at(i).inputname + loadedDevices.at(i).outputame);
            }
            else{
                m_lib->m_AudioRouter->addOfflineAudioDevice(loadedDevices.at(i).inputname,loadedDevices.at(i).outputame, loadedDevices.at(i).uid);
                m_lib->m_Log->writeLog(1,QString("loadIODevConfig: Error loading sound Device: device not found"));
            }
        }
        if(loadedDevices.at(i).devicetype == TestToneGenerator){
            m_lib->m_AudioRouter->addToneGen(loadedDevices.at(i).genfrequency, loadedDevices.at(i).uid);
            m_lib->m_Log->writeLog(3,QString("loadIODevConfig: added Generator from config file: ") + loadedDevices.at(i).inputname);

        }
        if(loadedDevices.at(i).devicetype == FilePlayer){
            m_lib->m_AudioRouter->addFilePlayer(loadedDevices.at(i).inputname, loadedDevices.at(i).path, loadedDevices.at(i).uid);
            m_lib->m_Log->writeLog(3,QString("loadIODevConfig: added FilePlayer from config file: ") + loadedDevices.at(i).inputname);
        }
        if(loadedDevices.at(i).devicetype == FileRecorder){
            m_lib->m_AudioRouter->addFileRecorder(loadedDevices.at(i).path, loadedDevices.at(i).uid);
            m_lib->m_Log->writeLog(3,QString("loadIODevConfig: added FileRecorder from config file: ") + loadedDevices.at(i).outputame);
        }
    }
}

void Settings::saveIODevConfig()
{
    QSettings settings("awah", "AWAHsipConfig");
    settings.setValue("IODevConfig", QVariant::fromValue(*m_lib->m_AudioRouter->getAudioDevices()));
    settings.sync();
}

void Settings::loadAccConfig()
{
    QList<s_account>  loadedAccounts;
    QSettings settings("awah", "AWAHsipConfig");
    loadedAccounts = settings.value("AccountConfig").value<QList<s_account>>();

    for( int i=0; i<loadedAccounts.count(); ++i ){
        m_lib->m_Accounts->createAccount(loadedAccounts.at(i).name,loadedAccounts.at(i).serverURI,loadedAccounts.at(i).user,loadedAccounts.at(i).password, loadedAccounts.at(i).FilePlayPath, loadedAccounts.at(i).FileRecordPath,loadedAccounts.at(i).fixedJitterBuffer,loadedAccounts.at(i).fixedJitterBufferValue,loadedAccounts.at(i).CallHistory, loadedAccounts.at(i).uid);
        m_lib->m_Log->writeLog(3,QString("loadAccConfig: added Account from config file: ") + loadedAccounts.at(i).name);
    }

}

void Settings::saveAccConfig()
{
    QSettings settings("awah", "AWAHsipConfig");
    settings.setValue("AccountConfig", QVariant::fromValue(*m_lib->m_Accounts->getAccounts()));
    settings.sync();
}

int Settings::loadAudioRoutes()
{
    int status = PJ_SUCCESS;
    QList<s_audioRoutes>  loadedRoutes;
    const QMap<int, QString> srcAudioSlotMap = m_lib->m_AudioRouter->getSrcAudioSlotMap();
    const QMap<int, QString> destAudioSlotMap = m_lib->m_AudioRouter->getDestAudioSlotMap();
    QSettings settings("awah", "AWAHsipConfig");

    loadedRoutes = settings.value("AudioRoutes").value<QList<s_audioRoutes>>();
    m_lib->m_Log->writeLog(3,QString("loadAudioRoutes: loaded routes: ") + QString::number(loadedRoutes.count()));


    for(auto& route : loadedRoutes ){
        route.srcSlot = srcAudioSlotMap.key(route.srcDevName, -1);
        route.destSlot = destAudioSlotMap.key(route.destDevName, -1);
        if(route.srcSlot >= 0 && route.destSlot >= 0){
            int check = m_lib->m_AudioRouter->connectConfPort(route.srcSlot, route.destSlot, route.level, route.persistant);
            if(check != PJ_SUCCESS)
                status = -1;
            else
                m_lib->m_Log->writeLog(3,QString("loadAudioRoutes: added AudioRoute from: ") + route.srcDevName + " to " + route.destDevName);
        } else {
            offlineRoutes.append(route);
            status = -1;
        }
    }
    saveAudioRoutes();
    return status;
}

int Settings::saveAudioRoutes()
{
    QList<s_audioRoutes> routesToSave, audioRoutes = m_lib->m_AudioRouter->getAudioRoutes();
    for(auto& route : audioRoutes){
        if(route.persistant)
            routesToSave.append(route);
    }
    routesToSave.append(offlineRoutes);
    QSettings settings("awah", "AWAHsipConfig");
    settings.setValue("AudioRoutes", QVariant::fromValue(routesToSave));
    settings.sync();
    return PJ_SUCCESS;
}

void Settings::loadSettings()                                           // todo check if library is alredy running and restart if true
{
    AccountConfig aCfg;
    QSettings settings("awah", "AWAHsipConfig");
    QJsonObject item, GlobalSettings, AudioSettings, SIPSettings, enumitems;

    // ******************** global endpoint settings *******************
    item = QJsonObject();
    m_lib->tCfg.port =  settings.value("settings/TransportConfig/Port","5060").toInt();
    item["value"] =  settings.value("settings/TransportConfig/Port","5060").toInt();
    item["type"] = INTEGER;
    item["min"] = 1;
    item["max"] = 65535;
    SIPSettings["SIP: Port"] = item;

    // ***** transport *****
    item = QJsonObject();
    m_lib->TransportProtocol = settings.value("settings/TransportConfig/Protocol","tcp").toString();
    if(settings.value("settings/TransportConfig/Protocol","tcp").toString() == "udp"){
        item["value"]  = 1;
    }
    else{
       item["value"] = 0;
    }
    item["type"] = ENUM;
    enumitems = QJsonObject();
    enumitems["tcp"] = "0";
    enumitems["udp"] = "1";
    item["enumlist"] = enumitems;
    SIPSettings["SIP: Transport protocol"] = item;

    // ***** Max calls *****
    item = QJsonObject();
    m_lib->epCfg.uaConfig.maxCalls = settings.value("settings/UserAgentConfig/MaxCalls","8").toInt();
    item["value"] = settings.value("settings/UserAgentConfig/MaxCalls","8").toInt();
    item["type"] = INTEGER;
    item["min"] = 1;
    item["max"] = 255;
    GlobalSettings["Max Calls"] = item;

    // ***** Channel count *****
    item = QJsonObject();
    item["type"] = INTEGER;
    m_lib->epCfg.medConfig.channelCount = settings.value("settings/MediaConfig/ChannelCount","2").toInt();
    item["value"] = settings.value("settings/MediaConfig/ChannelCount","2").toInt();
    item["min"] = 1;
    item["max"] = 8;
    GlobalSettings["Router max channel"] = item;

    // ***** ptime *****
    item = QJsonObject();
    item["type"] = INTEGER;
    m_lib->epCfg.medConfig.audioFramePtime = settings.value("settings/MediaConfig/Audio_Frame_Ptime","20").toInt();
    item["value"] = settings.value("settings/MediaConfig/Audio_Frame_Ptime","20").toInt();
    item["min"] = 10;
    item["max"] = 200;
    GlobalSettings["Audio frame packet time "] = item;

    // ***** Max Call Duration *****
    item = QJsonObject();
    item["type"] = INTEGER;
    m_lib->m_Accounts->m_MaxCallTime = settings.value("settings/MediaConfig/Max_Call_Duration","0").toInt();
    item["value"] = settings.value("settings/MediaConfig/Max_Call_Duration","0").toInt();
    item["min"] = 0;
    item["max"] = 7200;
    GlobalSettings["Max call duration (min)"] = item;


    // ***** clockrate *****
    item = QJsonObject();
    m_lib->epCfg.medConfig.clockRate = settings.value("settings/MediaConfig/Conference_Bridge_Clock_Rate","48000").toInt();
    item["value"] = settings.value("settings/MediaConfig/Conference_Bridge_Clock_Rate","48000").toInt();
    item["type"] = ENUM;
    item["min"] = 8000;
    item["max"] = 48000;
    enumitems = QJsonObject();
    enumitems[" 8 kHz"] = 8000;
    enumitems["16 kHz"] = 16000;
    enumitems["24 kHz"] = 24000;
    enumitems["32 kHz"] = 32000;
    enumitems["44.1 kHz"] = 44100;
    enumitems["48 kHz"] = 48000;
    item["enumlist"] = enumitems;
    AudioSettings["Router Clock Rate"] = item;

    // ***** echo canceleler ****
    item = QJsonObject();
    m_lib->epCfg.medConfig.ecTailLen = settings.value("settings/MediaConfig/Echo_Cancel_Tail","0").toInt();
    item["value"] = settings.value("settings/MediaConfig/Echo_Cancel_Tail","0").toInt();
    item["type"] = INTEGER;
    item["min"] = 0;
    item["max"] = 500;
    AudioSettings["Echo canceler tail lenght (0 for off)"] = item;

    // ***** jitter buffer *****
    item = QJsonObject();
    item["value"]  = m_lib->epCfg.medConfig.jbMax = settings.value("settings/MediaConfig/Jitter_Buffer_Max","-1").toInt();
    item["type"] = INTEGER;
    item["min"] = (int) m_lib->epCfg.medConfig.audioFramePtime;
    item["max"] = 1000;
    AudioSettings["Jitterbuffer max in ms"] = item;

    item = QJsonObject();
    item["value"]  = m_lib->epCfg.medConfig.jbMaxPre = settings.value("settings/MediaConfig/Jitter_Buffer_max_pre_delay","-1").toInt();
    item["type"] = INTEGER;
    item["min"] = (int) m_lib->epCfg.medConfig.audioFramePtime;
    item["max"] = 1000;
    AudioSettings["Jitterbuffer max prefetch delay ms"] = item;

    item = QJsonObject();
    item["value"]  = m_lib->epCfg.medConfig.jbMinPre = settings.value("settings/MediaConfig/Jitter_Buffer_min_pre_delay","-1").toInt();
    item["type"] = INTEGER;
    item["min"] = (int) m_lib->epCfg.medConfig.audioFramePtime;
    item["max"] = 1000;
    AudioSettings["Jitterbuffer min prefetch delay ms"] = item;

    item = QJsonObject();
    item["value"]  = m_lib->epCfg.medConfig.jbInit = settings.value("settings/MediaConfig/Jitter_Buffer_init_pre_delay","-1").toInt();
    item["type"] = INTEGER;
    item["min"] = m_lib->epCfg.medConfig.jbMinPre ;
    item["max"] = m_lib->epCfg.medConfig.jbMaxPre;
    AudioSettings["Jitterbuffer initial prefetch delay ms"] = item;


    // ***** sound dev clock rate *****
    item = QJsonObject();
    m_lib->epCfg.medConfig.sndClockRate = settings.value("settings/MediaConfig/Sound_Device_Clock_Rate","0").toInt();
    item["value"]  = settings.value("settings/MediaConfig/Sound_Device_Clock_Rate","0").toInt();
    item["type"] = ENUM;
    item["min"] = 8000;
    item["max"] = 48000;
    enumitems = QJsonObject();
    enumitems["follow router clockrate"] = 0;
    enumitems[" 8 kHz"] = 8000;
    enumitems["16 kHz"] = 16000;
    enumitems["24 kHz"] = 24000;
    enumitems["32 kHz"] = 32000;
    enumitems["44.1 kHz"] = 44100;
    enumitems["48 kHz"] = 48000;
    item["enumlist"] = enumitems;
    AudioSettings["Sound device clock Rate"] = item;

    // **** sound dev playback buffer *****
    item = QJsonObject();
    m_lib->epCfg.medConfig.sndPlayLatency = settings.value("settings/MediaConfig/Sound_Device_Playback_Latency","10").toInt();
    item["value"] = settings.value("settings/MediaConfig/Sound_Device_Playback_Latency","10").toInt();
    item["type"] = INTEGER;
    item["min"] = 1;
    item["max"] = 200;
    AudioSettings["Sound device playback buffer in ms "] = item;

    // ***** sound dev record buffer *****
    item = QJsonObject();
    m_lib->epCfg.medConfig.sndRecLatency = settings.value("settings/MediaConfig/Sound_Device_Record_Latency","10").toInt();
    item["value"] = settings.value("settings/MediaConfig/Sound_Device_Record_Latency","10").toInt();
    item["type"] = INTEGER;
    item["min"] = 1;
    item["max"] = 200;
    AudioSettings["Sound device record buffer in ms "] = item;

    // ***** log level *****
    item = QJsonObject();
    m_lib->epCfg.logConfig.consoleLevel = settings.value("settings/Loglevel","3").toInt();
    item["value"] = settings.value("settings/Loglevel","3").toInt();
    item["type"] = ENUM;
    item["min"] = 1;
    item["max"] = 5;
    enumitems = QJsonObject();
    enumitems["0 Display fatal error only"] = 0;
    enumitems["1 Display error messages"] = 1;
    enumitems["2 Display Warning messages"] = 2;
    enumitems["3 Info verbosity"] = 3;
    enumitems["4 Important events"] = 4;
    enumitems["5 Detailed events"] = 5;
    enumitems["6 Very detailed events"] = 6;
    item["enumlist"] = enumitems;
    GlobalSettings["Log level: "] = item;

    // ***** log path ****
    item = QJsonObject();
    item["value"]  = settings.value("settings/log/Path", QDir::current().filePath("logs/")).toString();
    item["type"] = STRING;
    GlobalSettings["Log path: "] = item;


    // ***** Account retry interval *****
    item = QJsonObject();
    aCfg.regConfig.retryIntervalSec = settings.value("settings/AcccountConfig/retryIntervalSec",30).toInt();
    item["value"] = settings.value("settings/AcccountConfig/retryIntervalSec",30).toInt();
    item["type"] = INTEGER;
    item["min"] = 30;
    item["max"] = 3600;
    GlobalSettings["Account retry interval in s"] = item;

    // ***** session timer expiration *****
    item = QJsonObject();
    aCfg.callConfig.timerSessExpiresSec = settings.value("settings/AcccountConfig/timersSesExpire", 1800).toInt();
    item["value"]  = settings.value("settings/AcccountConfig/timersSesExpire", 1800).toInt();
    item["type"] = INTEGER;
    item["min"] = (int) aCfg.callConfig.timerMinSESec;
    item["max"] = 3600;
    SIPSettings["Account session timer expiration"] = item;

    // ***** enable ICE *****
    item = QJsonObject();
    item["value"] = aCfg.natConfig.iceEnabled = settings.value("settings/NatConfig/Ice_Enabled",0).toBool();
    item["type"] = ENUM;
    item["min"] = 0;
    item["max"] = 1;
    enumitems = QJsonObject();
    enumitems["enabled"] = 1;
    enumitems["disabled"] = 0;
    item["enumlist"] = enumitems;
    SIPSettings["ICE (Interactive Connectivity Establishment)"] = item;

    // ***** ICE aressive nomination *****
    item = QJsonObject();
    item["value"]  = aCfg.natConfig.iceAggressiveNomination = settings.value("settings/NatConfig/Ice_AggressiveNomination","1").toBool();
    item["type"] = ENUM;
    item["min"] = 0;
    item["max"] = 1;
    enumitems = QJsonObject();
    enumitems["enabled"] = 1;
    enumitems["disabled"] = 0;
    item["enumlist"] = enumitems;
    SIPSettings["ICE agressive nomination"] = item;

    // ***** ICE always update *****
    item = QJsonObject();
    item["value"]  = aCfg.natConfig.iceAlwaysUpdate = settings.value("settings/NatConfig/Ice_AlwaysUpdate","0").toBool();
    item["type"] = ENUM;
    item["min"] = 0;
    item["max"] = 1;
    enumitems = QJsonObject();
    enumitems["enabled"] = 1;
    enumitems["disabled"] = 0;
    item["enumlist"] = enumitems;
    SIPSettings["ICE always update"] = item;

    // ***** ICE max hosts *****
    item = QJsonObject();
    item["value"]  = aCfg.natConfig.iceMaxHostCands = settings.value("settings/NatConfig/Ice_MaxHostCands","16").toInt();
    item["type"] = INTEGER;
    item["min"] = -1;
    item["max"] = 20;
    SIPSettings["ICE max host candidates"] = item;

    // ***** ICE nominated check delay *****
    item = QJsonObject();
    aCfg.natConfig.iceNominatedCheckDelayMsec = settings.value("settings/NatConfig/Ice_NominatedCheckDelayMsec","1000").toUInt();
    item["value"] = settings.value("settings/NatConfig/Ice_NominatedCheckDelayMsec","1000").toInt();
    item["type"] = INTEGER;
    item["min"] = -1;
    item["max"] = 5000;
    SIPSettings["ICE nominated check delay ms"] = item;


    // ***** ICE no Rtcp *****
    item = QJsonObject();
    item["value"]  = aCfg.natConfig.iceNoRtcp = settings.value("settings/NatConfig/Ice_NoRtcp","1").toBool();
    item["type"] = ENUM;
    item["min"] = 0;
    item["max"] = 1;
    enumitems = QJsonObject();
    enumitems["enabled"] = 0;
    enumitems["disabled"] = 1;
    item["enumlist"] = enumitems;
    SIPSettings["ICE Rtcp"] = item;

    // ***** ICE nomination timeout *****
    item = QJsonObject();
    item["value"] = aCfg.natConfig.iceWaitNominationTimeoutMsec = settings.value("settings/NatConfig/Ice_WaitNominationTimeoutMsec","1000").toInt();
    item["type"] = INTEGER;
    item["min"] = -1;
    item["max"] = 50000;
    SIPSettings["ICE wait nomination timeout ms"] = item;

    // ***** Media transport port *****
    item = QJsonObject();
    aCfg.mediaConfig.transportConfig.port = settings.value("settings/MediaConfig/Trasport_Port","5006").toInt();
    item["value"] = settings.value("settings/MediaConfig/Trasport_Port","5006").toInt();
    item["type"] = INTEGER;
    item["min"] = 1;
    item["max"] = 65535;
    SIPSettings["Media Config: transport port"] = item;

    // ***** STUN enable *****
    item = QJsonObject();
    item["value"]  = settings.value("settings/NatConfig/Enable_STUN","0").toBool();
    if (settings.value("settings/NatConfig/Enable_STUN","0").toBool())
    {
        aCfg.natConfig.sipStunUse = PJSUA_STUN_USE_DEFAULT;
        aCfg.natConfig.mediaStunUse = PJSUA_STUN_USE_DEFAULT;
    }
    else
    {
        aCfg.natConfig.sipStunUse = PJSUA_STUN_USE_DISABLED;
        aCfg.natConfig.mediaStunUse = PJSUA_STUN_USE_DISABLED;
    }
    item["type"] = ENUM;
    item["min"] = 0;
    item["max"] = 1;
    enumitems = QJsonObject();
    enumitems["enabled"] = 1;
    enumitems["disabled"] = 0;
    item["enumlist"] = enumitems;
    SIPSettings["STUN"] = item;

    // ***** Stun Server *****
    item = QJsonObject();
    item["type"] = STRING;
    item["value"] = settings.value("settings/NatConfig/STUN_Server","server URL").toString();
    if(settings.value("settings/NatConfig/Enable_STUN","0").toBool() == true){
        m_lib->epCfg.uaConfig.stunServer.push_back(settings.value("settings/NatConfig/STUN_Server","server URL").toString().toStdString());
    }
    SIPSettings["STUN Server URL:"] = item;

    // ***** Header-Rewrites *****
    item = QJsonObject();
    item["value"] = aCfg.natConfig.viaRewriteUse = settings.value("settings/NatConfig/SIPHeader_rewrite_Via","0").toBool();
    item["type"] = ENUM;
    item["min"] = 0;
    item["max"] = 1;
    enumitems = QJsonObject();
    enumitems["enabled"] = 1;
    enumitems["disabled"] = 0;
    item["enumlist"] = enumitems;
    SIPSettings["rewrite via in SIP header"] = item;

    item = QJsonObject();
    item["value"] = aCfg.natConfig.contactRewriteUse = settings.value("settings/NatConfig/SIPHeader_rewrite_Contact","1").toBool();
    item["type"] = ENUM;
    item["min"] = 0;
    item["max"] = 1;
    enumitems = QJsonObject();
    enumitems["enabled"] = 1;
    enumitems["disabled"] = 0;
    item["enumlist"] = enumitems;
    SIPSettings["rewrite contact in SIP header"] = item;


     // *************** Settings only in the config File, not exported to the GUI *********************

    // ***** min session timer expiration *****
    aCfg.callConfig.timerMinSESec = settings.value("settings/AcccountConfig/timerMinSESec", 90).toInt();
    settings.setValue("settings/AcccountConfig/timerMinSESec", aCfg.callConfig.timerMinSESec);
    m_lib->m_Accounts->setDefaultACfg(aCfg);

    // ***** Transport Port Range *****
    if(settings.value("settings/NatConfig/Ice_NoRtcp","1").toBool())               // range 0 creates a range of 10 adresses!
    {                                                                   // range 1 creates 2 adresses
        aCfg.mediaConfig.transportConfig.portRange = 0;
    }
    else
    {
    aCfg.mediaConfig.transportConfig.portRange = 2;
    // range has to be 2 if RTCP is on (but the audio port toggles between odd and even   NOT EBU Tech 3326 compliant!!!
    }

    //Websocket Port
    m_lib->m_websocketPort = settings.value("settings/Websocket/Port", "2924").toUInt();
    settings.setValue("settings/Websocket/Port", m_lib->m_websocketPort);

    // ****************** Hard coded settings ********************
    m_lib->epCfg.medConfig.quality =10;
    m_lib->epCfg.medConfig.noVad = true;
    aCfg.callConfig.timerUse = PJSUA_SIP_TIMER_ALWAYS;
    aCfg.callConfig.timerSessExpiresSec = 90;
    aCfg.regConfig.randomRetryIntervalSec = 10;                     // not all account schould reregister on the same time
    m_settings["SIPSettings"] = SIPSettings;
    m_settings["GlobalSettings"] = GlobalSettings;
    m_settings["AudioSettings"] =  AudioSettings;
}

QString Settings::getLogPath()
{
    QSettings settings("awah", "AWAHsipConfig");
    return settings.value("settings/log/Path", QDir::current().filePath("logs/")).toString();
}

const QJsonObject *Settings::getSettings()
{
    return &m_settings;
}


void Settings::setSettings(QJsonObject editedSettings)
{
    QSettings settings("awah", "AWAHsipConfig");
    QJsonObject::iterator it;
    for (it = editedSettings.begin(); it != editedSettings.end(); ++it) {
        if (it.key() == "SIP: Port"){
            settings.setValue("settings/TransportConfig/Port",it.value().toInt());
        }

        if (it.key() == "SIP: Transport protocol"){
            if(it.value().toInt()){
                settings.setValue("settings/TransportConfig/Protocol", "udp");
            }
            else{
                settings.setValue("settings/TransportConfig/Protocol", "tcp");
            }
        }

        if (it.key() == "Max call duration (min)"){
            settings.setValue("settings/MediaConfig/Max_Call_Duration",it.value().toInt());
        }

        if (it.key() == "Router max channel"){
            settings.setValue("settings/MediaConfig/ChannelCount",it.value().toInt());
        }

        if (it.key() == "Audio frame packet time "){
            settings.setValue("settings/MediaConfig/Audio_Frame_Ptime",it.value().toInt());
        }

        if (it.key() == "Router Clock Rate"){
             settings.setValue("settings/MediaConfig/Conference_Bridge_Clock_Rate",it.value().toInt());
        }

        if (it.key() == "Echo canceler tail lenght (0 for off)"){
             settings.setValue("settings/MediaConfig/Echo_Cancel_Tail",it.value().toInt());
        }

        if (it.key() == "Auto hang up when silence "){
             settings.setValue("settings/MediaConfig/Disable_autohangup_when_silence",it.value().toInt());
        }

        if (it.key() == "Jitterbuffer max in ms"){
             settings.setValue("settings/MediaConfig/Jitter_Buffer_Max",it.value().toInt());
        }

        if (it.key() == "Jitterbuffer max prefetch delay ms"){
            settings.setValue("settings/MediaConfig/Jitter_Buffer_max_pre_delay",it.value().toInt());
        }

        if (it.key() == "Jitterbuffer min prefetch delay ms"){
             settings.setValue("settings/MediaConfig/Jitter_Buffer_min_pre_delay",it.value().toInt());
        }

        if (it.key() == "Jitterbuffer initial prefetch delay ms"){
             settings.setValue("settings/MediaConfig/Jitter_Buffer_init_pre_delay",it.value().toInt());
        }

        if (it.key() == "Sound device clock Rate"){
             settings.setValue("settings/MediaConfig/Sound_Device_Clock_Rate",it.value().toInt());
        }

        if (it.key() == "Sound device playback buffer in ms "){
             settings.setValue("settings/MediaConfig/Sound_Device_Playback_Latency",it.value().toInt());
        }

        if (it.key() == "Sound device record buffer in ms "){
             settings.setValue("settings/MediaConfig/Sound_Device_Record_Latency",it.value().toInt());
        }

        if (it.key() == "Log level: "){
             settings.setValue("settings/Loglevel",it.value().toInt());
        }

        if (it.key() == "Log path: "){
             settings.setValue("settings/log/Path",it.value().toString());
        }

        if (it.key() == "Account retry interval in s"){
            settings.setValue("settings/AcccountConfig/retryIntervalSec",it.value().toInt());
        }

        if (it.key() == "Account session timer expiration"){
            settings.setValue("settings/AcccountConfig/timersSesExpire", it.value().toInt());
        }

        if (it.key() == "ICE (Interactive Connectivity Establishment)"){
            settings.setValue("settings/NatConfig/Ice_Enabled", it.value().toInt());
        }

        if (it.key() == "ICE agressive nomination"){
            settings.setValue("settings/NatConfig/Ice_AggressiveNomination", it.value().toInt());
        }

        if (it.key() == "ICE aalways update"){
            settings.setValue("settings/NatConfig/Ice_AlwaysUpdate", it.value().toInt());
        }

        if (it.key() == "ICE max host candidates"){
            settings.setValue("settings/NatConfig/Ice_MaxHostCands", it.value().toInt());
        }

        if (it.key() == "ICE nominated check delay ms"){
            settings.setValue("settings/NatConfig/Ice_NominatedCheckDelayMsec",it.value().toInt());
        }

        if (it.key() == "ICE Rtcp"){
             settings.setValue("settings/NatConfig/Ice_NoRtcp",it.value().toInt());
        }

        if (it.key() == "ICE wait nomination timeout ms"){
            settings.setValue("settings/NatConfig/Ice_WaitNominationTimeoutMsec",it.value().toInt());
        }

        if (it.key() == "Media Config: transport port"){
            settings.setValue("settings/MediaConfig/Trasport_Port",  it.value().toInt());
        }

        if (it.key() == "STUN"){
             settings.setValue("settings/NatConfig/Enable_STUN",  it.value().toInt());
        }

        if (it.key() == "STUN Server URL:"){
             settings.setValue("settings/NatConfig/STUN_Server", it.value().toString());
        }

        if (it.key() == "rewrite via in SIP header"){
             settings.setValue("settings/NatConfig/SIPHeader_rewrite_Via", it.value().toInt());
        }

        if (it.key() == "rewrite contact in SIP header"){
            settings.setValue("settings/NatConfig/SIPHeader_rewrite_Contact", it.value().toInt());
        }

    }
    settings.sync();
    loadSettings();
}

const QJsonObject Settings::getCodecPriorities(){
     QSettings settings("awah", "AWAHsipConfig");
     QJsonObject codecprios, enumitems, item, codecitem;
     int priority;
     QString codecname;
     enumitems["4 highest"] = 255;
     enumitems["3 higher"] = 254;
     enumitems["2 normal"] = 128;
     enumitems["1 lowest"] = 1;
     enumitems["0 disabled"] = 0;

     foreach(const CodecInfo codec, m_lib->m_pjEp->codecEnum2())
     {
         codecname = QString::fromStdString(codec.codecId);
         priority = settings.value("settings/CodecPriority/"+codecname,"128").toInt();
         codecitem = QJsonObject();
         codecitem["type"] = ENUM;
         codecitem["enumlist"] = enumitems;
         codecitem["value"] = priority;
         codecitem["min"] = 0;
         codecitem["max"]= 255;
         codecprios[codecname] = codecitem;
     }
     settings.sync();
     return codecprios;
}


void Settings::setCodecPriorities(QJsonObject CodecPriorities){

    QSettings settings("awah", "AWAHsipConfig");
    QJsonObject::iterator i;
    for (i =  CodecPriorities.begin(); i !=  CodecPriorities.end(); ++i) {
        settings.setValue("settings/CodecPriority/"+i.key(),i.value().toInt());
    }
    settings.sync();
    m_lib->listCodec();     // sets priorities to the endpoint
}
