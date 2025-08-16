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

#include "../include/settings.h"
#include "../include/awahsiplib.h"
#include "pjmedia.h"
#include "pjlib-util.h" /* pj_getopt */
#include "pjlib.h"
#include <QDebug>
#include <QThread>
#include <QSettings>
#include <QDir>

#define THIS_FILE		"settings.cpp"
#define CONFIG_FILE      "AWAHsipConfig.json"
#define CONFIG_DIR       ".config/awah"

Settings::Settings(AWAHSipLib *parentLib, QObject *parent) : QObject(parent), m_lib(parentLib)
{
}

QJsonObject Settings::loadJsonConfig()
{
    QString settingsPath = QSettings(QSettings::IniFormat, QSettings::UserScope, "awah", "AWAHsipConfig").fileName();
    m_lib->m_Log->writeLog(3, QString("Config file location: ") + settingsPath);

    QFile file(settingsPath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lib->m_Log->writeLog(3, QString("loadJsonConfig: No config file found, creating new configuration"));
        return QJsonObject();
    }

    QByteArray jsonData = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isNull()) {
        m_lib->m_Log->writeLog(1, QString("loadJsonConfig: Invalid JSON in config"));
        return QJsonObject();
    }
    
    return doc.object();
}

void Settings::saveJsonConfig(const QJsonObject &config)
{
    QString settingsPath = QSettings(QSettings::IniFormat, QSettings::UserScope, "awah", "AWAHsipConfig").fileName();
    QJsonDocument doc(config);
    
    QFile file(settingsPath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lib->m_Log->writeLog(1, QString("saveJsonConfig: Could not open config file for writing"));
        return;
    }
    
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    
    m_lib->m_Log->writeLog(3, QString("saveJsonConfig: Configuration saved to ") + settingsPath);
}

void Settings::loadIODevConfig()
{
    QList<s_IODevices> loadedDevices;
    int recordDevId, playbackDevId;

    // Load JSON configuration
    QJsonObject config = loadJsonConfig();
    QJsonArray deviceArray = config["IODevConfig"].toArray();
    
    // Convert JSON array to QList<s_IODevices>
    for (const QJsonValue &value : deviceArray) {
        QJsonObject obj = value.toObject();
        // Nur wenn die minimal benötigten Felder vorhanden sind
        if (obj.contains("uid") && obj.contains("devicetype")) {
            s_IODevices device;
            device.uid = obj["uid"].toString();
            device.devicetype = static_cast<DeviceType>(obj["devicetype"].toInt());
            // Optional fields - use defaults if not present
            device.inputname = obj.contains("inputname") ? obj["inputname"].toString() : QString();
            device.outputame = obj.contains("outputname") ? obj["outputname"].toString() : QString();
            device.path = obj.contains("path") ? obj["path"].toString() : QString();
            device.genfrequency = obj.contains("genfrequency") ? obj["genfrequency"].toInt() : 0;
            loadedDevices.append(device);
        }
    }

    m_lib->m_Log->writeLog(3, QString("loadIODevConfig: Settings loaded from JSON file"));
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
        
    // Load existing configuration
    QJsonObject config = loadJsonConfig();
    
    // Convert QList<s_IODevices> to JSON array
    QJsonArray deviceArray;
    const QList<s_IODevices>* devices = m_lib->m_AudioRouter->getAudioDevices();
    for (const s_IODevices &device : *devices) {
        QJsonObject obj;
        obj["uid"] = device.uid;
        obj["devicetype"] = static_cast<int>(device.devicetype);
        obj["inputname"] = device.inputname;
        obj["outputname"] = device.outputame;
        obj["path"] = device.path;
        obj["genfrequency"] = device.genfrequency;
        deviceArray.append(obj);
    }
    
    config["IODevConfig"] = deviceArray;
    
    // Save updated configuration
    saveJsonConfig(config);
}

void Settings::loadGpioDevConfig()
{
    QTimer::singleShot(1000, this, SLOT(loadIODevConfigLater()));                    // todo there must be a better way to do this
}

void Settings::loadIODevConfigLater()
{
    QList<s_IODevices> loadedDevices;
    QJsonObject config = loadJsonConfig();
    QJsonArray deviceArray = config["GpioDevConfig"].toArray();

    // Convert JSON array to QList<s_IODevices>
    for (const QJsonValue &value : deviceArray) {
        QJsonObject obj = value.toObject();
        if (obj.contains("uid")) {
            s_IODevices device;
            device.uid = obj["uid"].toString();
            device.devicetype = static_cast<DeviceType>(obj["devicetype"].toInt());
            device.inputname = obj.contains("inputname") ? obj["inputname"].toString() : QString();
            device.outputame = obj.contains("outputname") ? obj["outputname"].toString() : QString();
            device.path = obj.contains("path") ? obj["path"].toString() : QString();
            device.genfrequency = obj.contains("genfrequency") ? obj["genfrequency"].toInt() : 0;
            loadedDevices.append(device);
            m_lib->m_GpioDeviceManager->createGeneric(device);
            m_lib->m_Log->writeLog(3,QString("loadGpioDevManager: added GPIO device from config file: ") + device.outputame);
        }
    }
    m_GpioDevicesLoaded = true;
    loadGpioRoutes();
}

void Settings::saveGpioDevConfig()
{
    if (!m_GpioDevicesLoaded)
        return;
        
    // Load existing configuration
    QJsonObject config = loadJsonConfig();
    
    // Convert QList<s_IODevices> to JSON array
    QJsonArray deviceArray;
    const QList<s_IODevices>& devices = m_lib->m_GpioDeviceManager->getGpioDevices();
    for (const s_IODevices &device : devices) {
        QJsonObject obj;
        obj["uid"] = device.uid;
        obj["devicetype"] = static_cast<int>(device.devicetype);
        obj["inputname"] = device.inputname;
        obj["outputname"] = device.outputame;
        obj["path"] = device.path;
        obj["genfrequency"] = device.genfrequency;
        deviceArray.append(obj);
    }
    
    config["GpioDevConfig"] = deviceArray;
    
    // Save updated configuration
    saveJsonConfig(config);
}

void Settings::loadGpioRoutes()
{
    QJsonObject config = loadJsonConfig();
    QJsonArray routeArray = config["GpioRoutes"].toArray();

    m_lib->m_Log->writeLog(3, QString("loadGpioRoutes: loaded routes: ") + QString::number(routeArray.size()));

    for (const QJsonValue &value : routeArray) {
        QJsonObject obj = value.toObject();
        if (obj.contains("srcSlotId") && obj.contains("destSlotId")) {
            QString srcSlotId = obj["srcSlotId"].toString();
            QString destSlotId = obj["destSlotId"].toString();
            bool inverted = obj["inverted"].toBool();
            bool persistant = obj["persistant"].toBool();
            
            m_lib->m_Log->writeLog(3, QString("loadGpioRoutes: loading route from %1 to %2").arg(srcSlotId, destSlotId));
            GpioRouter::instance()->connectGpioPort(srcSlotId, destSlotId, inverted, persistant);
        }
    }
    m_GpioRoutesLoaded = true;
}

void Settings::saveGpioRoutes()
{
    if(!m_GpioRoutesLoaded)
        return;
        
    QJsonObject config = loadJsonConfig();
    QJsonArray routeArray;
    
    const QList<s_gpioRoute> gpioRoutes = GpioRouter::instance()->getGpioRoutes();
    for (const s_gpioRoute &route : gpioRoutes) {
        if (route.persistant) {
            QJsonObject obj;
            obj["srcSlotId"] = route.srcSlotId;  // Store as string
            obj["destSlotId"] = route.destSlotId;
            obj["inverted"] = route.inverted;
            obj["persistant"] = route.persistant;
            routeArray.append(obj);
            m_lib->m_Log->writeLog(3, QString("saveGpioRoutes: saving route from %1 to %2").arg(route.srcSlotId, route.destSlotId));
        }
    }
    
    config["GpioRoutes"] = routeArray;
    saveJsonConfig(config);
    m_lib->m_Log->writeLog(3, QString("saveGpioRoutes: saved routes: ") + QString::number(routeArray.size()));
}

void Settings::loadBuddies()
{
    QList<s_buddy> loadedBuddies;
    QJsonObject config = loadJsonConfig();
    QJsonArray buddyArray = config["Buddies"].toArray();

    for (const QJsonValue &value : buddyArray) {
        QJsonObject obj = value.toObject();
        if (obj.contains("uid")) {
            s_buddy buddy;
            buddy.uid = obj["uid"].toString();
            buddy.buddyUrl = obj["buddyUrl"].toString();
            buddy.Name = obj["Name"].toString();
            buddy.accUid = obj["accUid"].toString();
            
            s_codec codec;
            codec.fromJSON(obj["codec"].toObject());
            buddy.codec = codec;
            
            m_lib->m_Buddies->addBuddy(buddy.buddyUrl, buddy.Name, buddy.accUid, buddy.codec.toJSON(), buddy.uid);
        }
    }
    
    m_lib->m_Log->writeLog(3, QString("loadBuddies: loaded buddies: ") + QString::number(buddyArray.size()));
    m_BuddiesLoaded = true;
}

void Settings::saveBuddies()
{
    if(!m_BuddiesLoaded)
        return;
        
    QJsonObject config = loadJsonConfig();
    QJsonArray buddyArray;
    
    const QList<s_buddy>* buddies = m_lib->m_Buddies->getBuddies();
    for (const s_buddy &buddy : *buddies) {
        QJsonObject obj;
        obj["uid"] = buddy.uid;
        obj["buddyUrl"] = buddy.buddyUrl;
        obj["Name"] = buddy.Name;
        obj["accUid"] = buddy.accUid;
        obj["codec"] = buddy.codec.toJSON();
        buddyArray.append(obj);
    }
    
    config["Buddies"] = buddyArray;
    saveJsonConfig(config);
}

void Settings::loadAccConfig()
{
    QJsonObject config = loadJsonConfig();
    QJsonArray accountArray = config["AccountConfig"].toArray();

    for (const QJsonValue &value : accountArray) {
        QJsonObject obj = value.toObject();
        if (obj.contains("uid")) {
            s_account acc;
            acc.uid = obj["uid"].toString();
            acc.name = obj["name"].toString();
            acc.serverURI = obj["serverURI"].toString();
            acc.user = obj["user"].toString();
            acc.password = obj["password"].toString();
            acc.FilePlayPath = obj["FilePlayPath"].toString();
            acc.FileRecordPath = obj["FileRecordPath"].toString();
            acc.FileRecordRXonly = obj["FileRecordRXonly"].toBool();
            acc.fixedJitterBuffer = obj["fixedJitterBuffer"].toBool();
            acc.fixedJitterBufferValue = obj["fixedJitterBufferValue"].toInt();
            acc.autoconnectToBuddyUID = obj["autoconnectToBuddyUID"].toString();
            acc.autoconnectEnable = obj["autoconnectEnable"].toBool();
            acc.hasDTMFGPIO = obj["hasDTMFGPIO"].toBool();
            
            // Konvertiere CallHistory Array
            QJsonArray historyArray = obj["CallHistory"].toArray();
            for (const QJsonValue &histVal : historyArray) {
                s_callHistory callHist;
                callHist.callUri = histVal["callUri"].toString();
                if (histVal.isObject()) {
                    QJsonObject histObj = histVal.toObject();
                    // Setze weitere Felder von s_callHistory, falls vorhanden
                    if (histObj.contains("codec")) {
                        s_codec codec;
                        codec.fromJSON(histObj["codec"].toObject());
                        callHist.codec = codec;
                    }
                }
                acc.CallHistory.append(callHist);
            }
            
            m_lib->m_Accounts->createAccount(acc.name, acc.serverURI, acc.user, acc.password, 
                                           acc.FilePlayPath, acc.FileRecordPath, acc.FileRecordRXonly,
                                           acc.fixedJitterBuffer, acc.fixedJitterBufferValue,
                                           acc.autoconnectToBuddyUID, acc.autoconnectEnable,
                                           acc.hasDTMFGPIO, acc.CallHistory, acc.uid);
                                           
            m_lib->m_Log->writeLog(3, QString("loadAccConfig: added Account from config file: ") + acc.name);
        }
    }
    m_AccountsLoaded = true;
}

void Settings::saveAccConfig()
{
    if(!m_AccountsLoaded)
        return;
        
    QJsonObject config = loadJsonConfig();
    QJsonArray accountArray;
    
    const QList<s_account>* accounts = m_lib->m_Accounts->getAccounts();
    for (const s_account &acc : *accounts) {
        QJsonObject obj;
        obj["uid"] = acc.uid;
        obj["name"] = acc.name;
        obj["serverURI"] = acc.serverURI;
        obj["user"] = acc.user;
        obj["password"] = acc.password;
        obj["FilePlayPath"] = acc.FilePlayPath;
        obj["FileRecordPath"] = acc.FileRecordPath;
        obj["FileRecordRXonly"] = acc.FileRecordRXonly;
        obj["fixedJitterBuffer"] = acc.fixedJitterBuffer;
        obj["fixedJitterBufferValue"] = static_cast<int>(acc.fixedJitterBufferValue);
        obj["autoconnectToBuddyUID"] = acc.autoconnectToBuddyUID;
        obj["autoconnectEnable"] = acc.autoconnectEnable;
        obj["hasDTMFGPIO"] = acc.hasDTMFGPIO;
        
        // Konvertiere CallHistory in JSON Array
        QJsonArray historyArray;
        for (const s_callHistory &hist : acc.CallHistory) {
            QJsonObject histObj;
            histObj["callUri"] = hist.callUri;
            histObj["codec"] = hist.codec.toJSON();
            historyArray.append(histObj);
        }
        obj["CallHistory"] = historyArray;
        
        accountArray.append(obj);
    }
    
    config["AccountConfig"] = accountArray;
    saveJsonConfig(config);
}

void Settings::loadWebRTCChannelConfig() {
    QList<s_webrtc_channel> loadedChannels;
    QSettings settings("awah", "AWAHsipConfig");
    loadedChannels = settings.value("WebRTCChannelConfig").value<QList<s_webrtc_channel>>();
    for (int i = 0; i < loadedChannels.count(); ++i) {
        m_lib->m_WebRTCChannels->createChannel(
            loadedChannels.at(i).id,
            loadedChannels.at(i).description,
            loadedChannels.at(i).enabled
        );
        m_lib->m_Log->writeLog(3, QString("loadWebRTCChannelConfig: added WebRTC channel from config file: ") + loadedChannels.at(i).id);
    }
    m_WebRTCChannelsLoaded = true;
}

void Settings::saveWebRTCChannelConfig() {
    if (!m_WebRTCChannelsLoaded)
        return;
    QSettings settings("awah", "AWAHsipConfig");
    settings.setValue("WebRTCChannelConfig", QVariant::fromValue(*m_lib->m_WebRTCChannels->getChannels()));
    settings.sync();
}

int Settings::loadAudioRoutes()
{
    int status = PJ_SUCCESS;
    QList<s_audioRoutes> loadedRoutes;
    const QMap<int, QString> srcAudioSlotMap = m_lib->m_AudioRouter->getSrcAudioSlotMap();
    const QMap<int, QString> destAudioSlotMap = m_lib->m_AudioRouter->getDestAudioSlotMap();
    
    QJsonObject config = loadJsonConfig();
    QJsonArray routeArray = config["AudioRoutes"].toArray();
    
    m_lib->m_AudioRouter->clearAllOfflineAudioRoutes();
    
    // Konvertiere JSON Array zu QList<s_audioRoutes>
    for (const QJsonValue &value : routeArray) {
        QJsonObject obj = value.toObject();
        if (obj.contains("srcDevName") && obj.contains("destDevName")) {
            s_audioRoutes route;
            route.srcDevName = obj["srcDevName"].toString();
            route.destDevName = obj["destDevName"].toString();
            route.level = obj["level"].toDouble();
            route.persistant = obj["persistant"].toBool();
            loadedRoutes.append(route);
        }
    }
    
    m_lib->m_Log->writeLog(3, QString("loadAudioRoutes: loaded routes: ") + QString::number(loadedRoutes.count()));
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
        
    QJsonObject config = loadJsonConfig();
    QJsonArray routeArray;
    QList<s_audioRoutes> routesToSave;
    QList<s_audioRoutes> audioRoutes = m_lib->m_AudioRouter->getAudioRoutes();
    
    // Sammle persistente Routes und prüfe auf Duplikate
    for(const auto& route : audioRoutes) {
        if(route.persistant) {
            bool routeExists = false;
            for(const auto& savedRoute : routesToSave) {
                if(route.destDevName == savedRoute.destDevName && route.srcDevName == savedRoute.srcDevName) {
                    routeExists = true;
                    break;
                }
            }
            if(!routeExists) {
                routesToSave.append(route);
            }
        }
    }
    
    // Füge Offline-Routes hinzu
    for(const auto& offlineroute : m_lib->m_AudioRouter->getOfflineAudioRoutes()) {
        if(offlineroute.persistant) {
            bool isOnline = false;
            for(const auto& route : audioRoutes) {
                if(route.srcDevName == offlineroute.srcDevName && route.destDevName == offlineroute.destDevName) {
                    isOnline = true;
                    break;
                }
            }
            if(!isOnline) {
                routesToSave.append(offlineroute);
            }
        }
    }
    
    // Konvertiere zu JSON
    for(const auto& route : routesToSave) {
        QJsonObject obj;
        obj["srcDevName"] = route.srcDevName;
        obj["destDevName"] = route.destDevName;
        obj["level"] = route.level;
        obj["persistant"] = route.persistant;
        routeArray.append(obj);
    }
    
    config["AudioRoutes"] = routeArray;
    saveJsonConfig(config);
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
                settings.setValue("settings/MediaConfig/Master_Clock", QString::fromUtf8(info.name));
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
