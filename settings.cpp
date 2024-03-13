/*
 * Copyright (C) 2016 - 2022 Andy Weiss, Adi Hilber
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
    QList<s_IODevices> loadedDevices;
    int recordDevId, playbackDevId;
    QSettings settings("awah", "AWAHsipConfig");
    m_lib->m_Log->writeLog(3,QString("loadConfig: Settingsloaded from file:" + settings.fileName()));
    loadedDevices = settings.value("IODevConfig").value<QList<s_IODevices>>();
    QString MasterClockDev = getMasterClock();

    bool clockdevFound = false;
    for( int i=0; i<loadedDevices.count(); ++i ){
        if(loadedDevices.at(i).devicetype == SoundDevice){                                      // add the Clocking Device first because the other devices get the clock from the port 0 of the conference bridge.
            if(loadedDevices.at(i).inputname == MasterClockDev){
                recordDevId = m_lib->m_AudioRouter->getSoundDevID(loadedDevices.at(i).inputname);
                playbackDevId = m_lib->m_AudioRouter->getSoundDevID(loadedDevices.at(i).outputame);
                m_lib->m_AudioRouter->AddClockingDevice(recordDevId,playbackDevId, loadedDevices.at(i).uid);
                m_lib->m_Log->writeLog(3,QString("loadIODevConfig: added Master clocking sound device form config file: ") + loadedDevices.at(i).inputname + " " + loadedDevices.at(i).outputame);
                if(recordDevId > -1){
                    clockdevFound = true;
                }
            }
        }
    }
    if(!clockdevFound){
        m_lib->m_pjEp->audDevManager().setNullDev();
        m_lib->m_Log->writeLog(3,QString("loadIODevConfig: Setting up dummy sound device for internal clocking"));
    }

    for( int i=0; i<loadedDevices.count(); ++i )                                                     // todo send an error message if sound device is not found!!
    {
        if(loadedDevices.at(i).devicetype == SoundDevice){
            recordDevId = m_lib->m_AudioRouter->getSoundDevID(loadedDevices.at(i).inputname);
            playbackDevId = m_lib->m_AudioRouter->getSoundDevID(loadedDevices.at(i).outputame);
            if (recordDevId != -1 && playbackDevId !=-1){
                m_lib->m_AudioRouter->addAudioDevice(recordDevId,playbackDevId, loadedDevices.at(i).uid);
                m_lib->m_Log->writeLog(3,QString("loadIODevConfig: added sound device from config file: ") + loadedDevices.at(i).inputname + " " + loadedDevices.at(i).outputame);
            }
            else{
                m_lib->m_AudioRouter->setAudioDeviceToOffline(loadedDevices.at(i).inputname,loadedDevices.at(i).outputame, loadedDevices.at(i).uid);
                m_lib->m_Log->writeLog(1,QString("loadIODevConfig: Error loading sound Device: ") + loadedDevices.at(i).inputname + loadedDevices.at(i).outputame + " device not found");
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
    m_IoDevicesLoaded = true;
}

void Settings::saveIODevConfig()
{
    if (!m_IoDevicesLoaded)
        return;
    QSettings settings("awah", "AWAHsipConfig");
    settings.setValue("IODevConfig", QVariant::fromValue(*m_lib->m_AudioRouter->getAudioDevices()));
    settings.sync();
}

void Settings::loadGpioDevConfig()
{
    QTimer::singleShot(1000, this, SLOT(loadIODevConfigLater()));                    // todo there must be a better way to do this
}

void Settings::loadIODevConfigLater()
{
    QList<s_IODevices> loadedDevices;
    QSettings settings("awah", "AWAHsipConfig");
    loadedDevices = settings.value("GpioDevConfig").value<QList<s_IODevices>>();
    for(auto& device : loadedDevices){
        m_lib->m_GpioDeviceManager->createGeneric(device);
        m_lib->m_Log->writeLog(3,QString("loadGpioDevManager: added GPIO device from config file: ") + device.outputame);
        }
    m_GpioDevicesLoaded = true;
    loadGpioRoutes();
}

void Settings::saveGpioDevConfig()
{
    if (!m_GpioDevicesLoaded)
        return;
    QSettings settings("awah", "AWAHsipConfig");
    settings.setValue("GpioDevConfig", QVariant::fromValue(m_lib->m_GpioDeviceManager->getGpioDevices()));
    settings.sync();
}

void Settings::loadGpioRoutes()
{
    QList<s_gpioRoute>  loadedRoutes;
    QSettings settings("awah", "AWAHsipConfig");

    loadedRoutes = settings.value("GpioRoutes").value<QList<s_gpioRoute>>();
    m_lib->m_Log->writeLog(3,QString("loadGpioRoutes: loaded routes: ") + QString::number(loadedRoutes.count()));

    for(auto& route : loadedRoutes ){
        GpioRouter::instance()->connectGpioPort(route.srcSlotId,route.destSlotId,route.inverted, route.persistant);
    }
    m_GpioRoutesLoaded = true;
}

void Settings::saveGpioRoutes()
{
    if(!m_GpioRoutesLoaded)
        return;
    QList<s_gpioRoute> routesToSave;
    const QList<s_gpioRoute> gpioRoutes = GpioRouter::instance()->getGpioRoutes();
    for(auto& route : gpioRoutes){
        if(route.persistant)
            routesToSave.append(route);
    }
    //routesToSave.append(offlineRoutes);                                   // todo remember offlie GPIO routes
    QSettings settings("awah", "AWAHsipConfig");
    settings.setValue("GpioRoutes", QVariant::fromValue(routesToSave));
    settings.sync();
}

void Settings::loadBuddies()
{
    QList<s_buddy>  loadedBuddies;
    QSettings settings("awah", "AWAHsipConfig");

    loadedBuddies = settings.value("Buddies").value<QList<s_buddy>>();
    m_lib->m_Log->writeLog(3,QString("loadBuddies: loaded buddies: ") + QString::number(loadedBuddies.count()));

    for(auto& buddy : loadedBuddies ){
        m_lib->m_Buddies->addBuddy(buddy.buddyUrl,buddy.Name,buddy.accUid,buddy.codec.toJSON(),buddy.uid);
    }
    m_BuddiesLoaded = true;
}

void Settings::saveBuddies()
{
    if(!m_BuddiesLoaded)
        return;
    QSettings settings("awah", "AWAHsipConfig");
    settings.setValue("Buddies", QVariant::fromValue(*m_lib->m_Buddies->getBuddies()));
    settings.sync();
}

void Settings::loadAccConfig()
{
    QList<s_account>  loadedAccounts;
    QSettings settings("awah", "AWAHsipConfig");
    loadedAccounts = settings.value("AccountConfig").value<QList<s_account>>();

    for(int i=0; i<loadedAccounts.count(); ++i ){
        m_lib->m_Accounts->createAccount(loadedAccounts.at(i).name,loadedAccounts.at(i).serverURI,loadedAccounts.at(i).user,loadedAccounts.at(i).password, loadedAccounts.at(i).FilePlayPath, loadedAccounts.at(i).FileRecordPath, loadedAccounts.at(i).FileRecordRXonly ,loadedAccounts.at(i).fixedJitterBuffer,loadedAccounts.at(i).fixedJitterBufferValue,loadedAccounts.at(i).autoconnectToBuddyUID, loadedAccounts.at(i).autoconnectEnable ,loadedAccounts.at(i).hasDTMFGPIO ,loadedAccounts.at(i).CallHistory, loadedAccounts.at(i).uid);
        m_lib->m_Log->writeLog(3,QString("loadAccConfig: added Account from config file: ") + loadedAccounts.at(i).name);
    }
    m_AccountsLoaded = true;
}

void Settings::saveAccConfig()
{
    if(!m_AccountsLoaded)
        return;
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
    m_lib->m_AudioRouter->clearAllOfflineAudioRoutes();
    loadedRoutes = settings.value("AudioRoutes").value<QList<s_audioRoutes>>();
    m_lib->m_Log->writeLog(3,QString("loadAudioRoutes: loaded routes: ") + QString::number(loadedRoutes.count()));
    for(auto& route : loadedRoutes ){
        route.srcSlot = srcAudioSlotMap.key(route.srcDevName, -1);
        route.destSlot = destAudioSlotMap.key(route.destDevName, -1);
        if(route.srcSlot >= 0 && route.destSlot >= 0){
            if(route.level < -42){                                             // this check is done to catch old volumes witch where stored in factors not dB!!
                route.level = -43;
            }
            if(route.level > 20){
                route.level = 20;
            }
            int check = m_lib->m_AudioRouter->connectConfPort(route.srcSlot, route.destSlot, route.level, route.persistant);
            if(check != PJ_SUCCESS)
                status = -1;
            else
                m_lib->m_Log->writeLog(3,QString("loadAudioRoutes: added AudioRoute from: ") + route.srcDevName + " to " + route.destDevName);
        } else {
            m_lib->m_AudioRouter->addOfflineAudioRoute(route);
            status = -1;
        }
    }
    m_AudioRoutesLoaded = true;
    saveAudioRoutes();
    return status;
}

int Settings::saveAudioRoutes()
{
    if(!m_AudioRoutesLoaded)
        return PJ_SUCCESS;
    QList<s_audioRoutes> routesToSave, audioRoutes = m_lib->m_AudioRouter->getAudioRoutes();
    for(auto& route : audioRoutes){
        if(route.persistant){
            bool routeExists = false;
            for(auto& savedRoute : routesToSave){                                                               // check if route already exists
                if(route.destDevName == savedRoute.destDevName && route.srcDevName == savedRoute.srcDevName){
                    routeExists = true;
                    break;
                }
            }
            if(!routeExists){
                routesToSave.append(route);
            }
        }
    }
    for(auto& offlineroute : m_lib->m_AudioRouter->getOfflineAudioRoutes()){
        if(offlineroute.persistant){
            for(auto& route : audioRoutes){
                if(route.srcDevName == offlineroute.srcDevName && route.destDevName == offlineroute.destDevName){  // check if the offline route is alredy stored as online route
                    break;
                }
                else{
                    routesToSave.append(offlineroute);
                }
            }
        }
    }
    QSettings settings("awah", "AWAHsipConfig");
    settings.setValue("AudioRoutes", QVariant::fromValue(routesToSave));
    settings.sync();
    return PJ_SUCCESS;
}

void Settings::saveCustomSourceNames()
{
    QSettings settings("awah", "AWAHsipConfig");
    settings.remove("CustomSourceNames");
    settings.beginGroup("CustomSourceNames");
    QMap<QString,QString> srcLables = m_lib->m_AudioRouter->getCustomSourceLabels();
    QMap<QString, QString>::const_iterator i = srcLables.constBegin();
    while (i != srcLables.constEnd()) {
         settings.setValue(i.key(), i.value());
         ++i;
     }
    settings.endGroup();
    settings.sync();
}

void Settings::loadCustomSourceNames()
{
    QSettings settings("awah", "AWAHsipConfig");
    settings.beginGroup("CustomSourceNames");
    QMap<QString,QString> srcLables;
    QStringList keys = settings.childKeys();
    foreach (QString key, keys) {
         srcLables[key] = settings.value(key).toString();
    }
    settings.endGroup();
    m_lib->m_Log->writeLog(3,QString("loadCustomSourceNames: custom source lables loaded"));
    m_lib->m_AudioRouter->setCustomSourceLables(srcLables);
}

void Settings::saveCustomDestinationNames()
{
    QSettings settings("awah", "AWAHsipConfig");
    settings.remove("CustomDestinationNames");
    settings.beginGroup("CustomDestinationNames");
    QMap<QString,QString> dstLables = m_lib->m_AudioRouter->getCustomDestLables();
    QMap<QString, QString>::const_iterator i = dstLables.constBegin();
    while (i != dstLables.constEnd()) {
         settings.setValue(i.key(), i.value());
         ++i;
     }
    settings.endGroup();
    settings.sync();
}

void Settings::loadCustomDestinationNames()
{
    QSettings settings("awah", "AWAHsipConfig");
    settings.beginGroup("CustomDestinationNames");
    QMap<QString,QString> dstLables;
    QStringList keys = settings.childKeys();
    foreach (QString key, keys) {
         dstLables[key] = settings.value(key).toString();
    }
    settings.endGroup();
    m_lib->m_AudioRouter->setCustomDestinationLables(dstLables);
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
    item["type"] = ENUM_INT;
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
    GlobalSettings["Audio frame packet time"] = item;

    // ***** Max Call Duration *****
    item = QJsonObject();
    item["type"] = INTEGER;
    m_lib->m_Accounts->m_MaxCallTime = settings.value("settings/MediaConfig/Max_Call_Duration","0").toInt();
    item["value"] = settings.value("settings/MediaConfig/Max_Call_Duration","0").toInt();
    item["min"] = 0;
    item["max"] = 7200;
    GlobalSettings["Max call duration (min)"] = item;

    // ******* Call disconnect timeout after media loss *********
    item = QJsonObject();
    item["type"] = INTEGER;
    m_lib->m_Accounts->m_CallDisconnectRXTimeout = settings.value("settings/MediaConfig/CallDisconnectRXTimeout","10").toInt();
    item["value"] = settings.value("settings/MediaConfig/CallDisconnectRXTimeout","10").toInt();
    item["min"] = 0;
    item["max"] = 7200;
    GlobalSettings["Call disconnect after RX lost in seconds"] = item;


    // ***** clockrate *****
    item = QJsonObject();
    m_lib->epCfg.medConfig.clockRate = settings.value("settings/MediaConfig/Conference_Bridge_Clock_Rate","48000").toInt();
    item["value"] = settings.value("settings/MediaConfig/Conference_Bridge_Clock_Rate","48000").toInt();
    item["type"] = ENUM_INT;
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
    item["type"] = ENUM_INT;
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
    AudioSettings["Sound device playback buffer in ms"] = item;

    // ***** sound dev record buffer *****
    item = QJsonObject();
    m_lib->epCfg.medConfig.sndRecLatency = settings.value("settings/MediaConfig/Sound_Device_Record_Latency","10").toInt();
    item["value"] = settings.value("settings/MediaConfig/Sound_Device_Record_Latency","10").toInt();
    item["type"] = INTEGER;
    item["min"] = 1;
    item["max"] = 200;
    AudioSettings["Sound device record buffer in ms"] = item;

    // ***** log level *****
    item = QJsonObject();
    m_lib->epCfg.logConfig.consoleLevel = settings.value("settings/Loglevel","3").toInt();
    item["value"] = settings.value("settings/Loglevel","3").toInt();
    item["type"] = ENUM_INT;
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
    GlobalSettings["Log level:"] = item;

    // ***** log path ****
    item = QJsonObject();
    item["value"]  = settings.value("settings/log/Path", QDir::current().filePath("logs/")).toString();
    item["type"] = STRING;
    GlobalSettings["Log path:"] = item;

    // ***** Buddy refresh interval *****
    item = QJsonObject();
    m_lib->m_Buddies->SetMaxPresenceRefreshTime(settings.value("settings/BuddyConfig/maxPresenceRefreshTime","30").toUInt());
    item["value"] = settings.value("settings/BuddyConfig/maxPresenceRefreshTime","30").toInt();
    item["type"] = INTEGER;
    item["min"] = 10;
    item["max"] = 500;
    GlobalSettings["Buddy presence refresh time"] = item;

    // ***** session timer expiration *****
    item = QJsonObject();
    aCfg.callConfig.timerSessExpiresSec = settings.value("settings/AcccountConfig/timersSesExpire", 1800).toInt();
    item["value"]  = settings.value("settings/AcccountConfig/timersSesExpire", 1800).toInt();
    item["type"] = INTEGER;
    item["min"] = (int) aCfg.callConfig.timerMinSESec;
    item["max"] = 3600;
    SIPSettings["Account session timer expiration"] = item;

    // ***** NAT hole pinching keep alive packets *********
    item = QJsonObject();
    aCfg.natConfig.udpKaIntervalSec = settings.value("settings/NatConfig/KaInterval",0).toInt();
    item["value"] = settings.value("settings/NatConfig/KaInterval",0).toInt();
    item["type"] = INTEGER;
    item["min"] = 0;
    item["max"] = 60;
    SIPSettings["NAT hole punching keep alive packet timer (sec)"] = item;

    // ***** retry registration failure delay *********
    item = QJsonObject();
    aCfg.regConfig.retryIntervalSec = settings.value("settings/regConfig/retryIntervalSec",35).toInt();
    item["value"] = settings.value("settings/regConfig/retryIntervalSec",35).toInt();
    item["type"] = INTEGER;
    item["min"] = 0;
    item["max"] = 1000;
    SIPSettings["auto registration retry upon registration failure (sec)"] = item;

    // ****** first retry delay on registration failure **************
    item = QJsonObject();
    aCfg.regConfig.firstRetryIntervalSec = settings.value("settings/regConfig/firstRetryIntervalSec",120).toInt();
    item["value"] = settings.value("settings/regConfig/firstRetryIntervalSec",120).toInt();
    item["type"] = INTEGER;
    item["min"] = 0;
    item["max"] = 1000;
    SIPSettings["interval for the first registration retry (sec)"] = item;


    // ***** enable ICE *****
    item = QJsonObject();
    item["value"] = aCfg.natConfig.iceEnabled = settings.value("settings/NatConfig/Ice_Enabled",0).toBool();
    item["type"] = ENUM_INT;
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
    item["type"] = ENUM_INT;
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
    item["type"] = ENUM_INT;
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
    item["type"] = ENUM_INT;
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
    item["type"] = ENUM_INT;
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
    item["type"] = ENUM_INT;
    item["min"] = 0;
    item["max"] = 1;
    enumitems = QJsonObject();
    enumitems["enabled"] = 1;
    enumitems["disabled"] = 0;
    item["enumlist"] = enumitems;
    SIPSettings["rewrite via in SIP header"] = item;

    item = QJsonObject();
    item["value"] = aCfg.natConfig.contactRewriteUse = settings.value("settings/NatConfig/SIPHeader_rewrite_Contact","1").toBool();
    item["type"] = ENUM_INT;
    item["min"] = 0;
    item["max"] = 1;
    enumitems = QJsonObject();
    enumitems["enabled"] = 1;
    enumitems["disabled"] = 0;
    item["enumlist"] = enumitems;
    SIPSettings["rewrite contact in SIP header"] = item;

    // ***** bound address *****
    TransportConfig	transportConfig;

    item = QJsonObject();
    item["type"] = STRING;
    item["value"] = settings.value("settings/MediaConfig/TrasportConfig/boundAddress","0.0.0.0").toString();
    transportConfig.boundAddress = settings.value("settings/MediaConfig/TrasportConfig/boundAddress","0.0.0.0").toString().toStdString();
    aCfg.mediaConfig.transportConfig.boundAddress.assign(transportConfig.boundAddress);
    SIPSettings["Transport bound address"] = item;

    item = QJsonObject();
    item["type"] = STRING;
    item["value"] = settings.value("settings/MediaConfig/TrasportConfig/publicAddress","0.0.0.0").toString();
    transportConfig.publicAddress = settings.value("settings/MediaConfig/TrasportConfig/publicAddress","0.0.0.0").toString().toStdString();
    aCfg.mediaConfig.transportConfig.publicAddress.assign(transportConfig.publicAddress);
    SIPSettings["Transport public address"] = item;

     // *************** Settings only in the config File, not exported to the GUI *********************

    // ***** min session timer expiration *****
    aCfg.callConfig.timerMinSESec = settings.value("settings/AcccountConfig/timerMinSESec", 90).toInt();
    settings.setValue("settings/AcccountConfig/timerMinSESec", aCfg.callConfig.timerMinSESec);

    // ***** Transport Port Range *****
//    if(settings.value("settings/NatConfig/Ice_NoRtcp","1").toBool())               // range 0 creates a range of 10 adresses!             // todo test me
//    {                                                                   // range 1 creates 2 adresses
//        aCfg.mediaConfig.transportConfig.portRange = 0;
//    }
//    else
//    {
//    aCfg.mediaConfig.transportConfig.portRange = 2;
//    // range has to be 2 if RTCP is on (but the audio port toggles between odd and even   NOT EBU Tech 3326 compliant!!!
//    }

    aCfg.regConfig.randomRetryIntervalSec = 10;             // not all account schould reregister on the same time
    //aCfg.ipChangeConfig.shutdownTp = 1;
    m_lib->epCfg.medConfig.quality =10;
    m_lib->epCfg.medConfig.noVad = true;
    m_lib->m_Accounts->setDefaultACfg(aCfg);
    //Websocket Port
    m_lib->m_websocketPort = settings.value("settings/Websocket/Port", "2924").toUInt();
    settings.setValue("settings/Websocket/Port", m_lib->m_websocketPort);

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
    getMasterClock();                       // this appends the MasterClock field to the settings.
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

        if (it.key() == "Call disconnect after RX lost in seconds"){
            settings.setValue("settings/MediaConfig/CallDisconnectRXTimeout",it.value().toInt());
        }

        if (it.key() == "Max Calls"){
            settings.setValue("settings/UserAgentConfig/MaxCalls", it.value().toInt());
        }

        if (it.key() == "Router max channel"){
            settings.setValue("settings/MediaConfig/ChannelCount",it.value().toInt());
        }

        if (it.key() == "Audio frame packet time"){
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

        if (it.key() == "Sound device playback buffer in ms"){
             settings.setValue("settings/MediaConfig/Sound_Device_Playback_Latency",it.value().toInt());
        }

        if (it.key() == "Sound device record buffer in ms"){
             settings.setValue("settings/MediaConfig/Sound_Device_Record_Latency",it.value().toInt());
        }

        if (it.key() == "Router clock source"){                                         // save the device name instead of the id beause the id can change on restart
            pjmedia_aud_dev_info info;
            pjmedia_aud_dev_get_info(it.value().toInt(),&info);
            if(it.value().toInt() == -1){
                settings.setValue("settings/MediaConfig/Master_Clock","internal");
            }
            else{
                settings.setValue("settings/MediaConfig/Master_Clock",info.name);
            }
        }

        if (it.key() == "Log level:"){
             settings.setValue("settings/Loglevel",it.value().toInt());
        }

        if (it.key() == "Log path:"){
             settings.setValue("settings/log/Path",it.value().toString());
        }

        if (it.key() == "Account session timer expiration"){
            settings.setValue("settings/AcccountConfig/timersSesExpire", it.value().toInt());
        }

        if (it.key() == "NAT hole punching keep alive packet timer (sec)"){
            settings.setValue("settings/NatConfig/KaInterval", it.value().toInt());
        }

        if (it.key() == "auto registration retry upon registration failure (sec)"){
            settings.setValue("settings/regConfig/retryIntervalSec", it.value().toInt());
        }

        if (it.key() == "interval for the first registration retry (sec)"){
            settings.setValue("settings/regConfig/firstRetryIntervalSec", it.value().toInt());
        }

        if (it.key() == "ICE (Interactive Connectivity Establishment)"){
            settings.setValue("settings/NatConfig/Ice_Enabled", it.value().toInt());
        }

        if (it.key() == "ICE agressive nomination"){
            settings.setValue("settings/NatConfig/Ice_AggressiveNomination", it.value().toInt());
        }

        if (it.key() == "ICE always update"){
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
        if (it.key() == "Buddy presence refresh time"){
            settings.setValue("settings/BuddyConfig/maxPresenceRefreshTime", it.value().toInt());
        }

        if (it.key() == "Transport bound address"){
            settings.setValue("settings/MediaConfig/TrasportConfig/boundAddress", it.value().toString());
        }
        if (it.key() == "Transport public address"){
            settings.setValue("settings/MediaConfig/TrasportConfig/publicAddress", it.value().toString());
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
         codecitem["type"] = ENUM_INT;
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
    m_lib->m_Codecs->listCodecs();     // sets priorities to the endpoint
}

QString Settings::getMasterClock(){                                 // master Clock of the internal Audio router, saved with setSettings funtion. This get Function is separate becaus it needs the library started to enum availabe sync sources
    QSettings settings("awah", "AWAHsipConfig");
    QJsonObject AudioSettings = m_settings["AudioSettings"].toObject(), enumitems, item, codecitem;
    item = QJsonObject();
    item["value"]  = m_lib->m_AudioRouter->getSoundDevID(settings.value("settings/MediaConfig/Master_Clock","internal").toString());
    item["type"] = ENUM_INT;
    item["min"] = -1;
    item["max"] = 127;
    enumitems = QJsonObject();
    enumitems["internal"] = -1;
    for (auto& Audiodev : *m_lib->m_AudioRouter->getAudioDevices()) {
        if(Audiodev.devicetype == SoundDevice && Audiodev.RecDevID > -1){
            enumitems[Audiodev.inputname] = Audiodev.RecDevID;
        }
    }
    item["enumlist"] = enumitems;
    AudioSettings["Router clock source"] = item;
    m_settings["AudioSettings"] = AudioSettings;
    return settings.value("settings/MediaConfig/Master_Clock","internal").toString();
}
