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
#include <QDebug>
#include <QThread>
#include <QSettings>
#include <QDir>
#include <QtCore/QCoreApplication>

#define THIS_FILE		"settings.cpp"

#define CONFIG_FILE      "AWAHsipConfig.json"
#define CONFIG_DIR       ".config/awah"

Settings::Settings(AWAHSipLib *parentLib, QObject *parent) : QObject(parent), m_lib(parentLib)
{
}

QJsonObject Settings::loadJsonConfig()
{
    // Load from AWAHsipConfig.ini file using QSettings path (contains pure JSON)
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "awah", "AWAHsipConfig");
    QString settingsPath = settings.fileName();

    // Log only once on first call
    static bool firstCall = true;
    if (firstCall) {
        m_lib->m_Log->writeLog(3, QString("Loading JSON config from: ") + settingsPath);
        firstCall = false;
    }

    QFile file(settingsPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lib->m_Log->writeLog(1, QString("Could not open config file: ") + settingsPath);
        m_lib->m_Log->writeLog(1, QString("Error: ") + file.errorString());
        return QJsonObject();
    }

    // Read the entire content and parse as json
    QByteArray jsonData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (doc.isNull()) {
        m_lib->m_Log->writeLog(1, QString("JSON parse error at position %1: %2")
            .arg(parseError.offset)
            .arg(parseError.errorString()));
        return QJsonObject();
    }
    QJsonObject obj = doc.object();
    
    return obj;
}

void Settings::saveJsonConfig(const QJsonObject &newConfig)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "awah", "AWAHsipConfig");
    QString settingsPath = settings.fileName();

    
    // Load existing configuration
    QJsonObject existingConfig;
    QFile readFile(settingsPath);
    if (readFile.open(QIODevice::ReadOnly)) {
        QByteArray data = readFile.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull() && doc.isObject()) {
            existingConfig = doc.object();
        }
        readFile.close();
    }
    
    // Merge new configuration with existing
    for (auto it = newConfig.begin(); it != newConfig.end(); ++it) {
        existingConfig[it.key()] = it.value();
    }
    
    
    // Save the merged configuration
    QJsonDocument doc(existingConfig);
    QFile writeFile(settingsPath);
    if (!writeFile.open(QIODevice::WriteOnly)) {
        m_lib->m_Log->writeLog(1, QString("Error: Could not open config file for writing: ") + settingsPath);
        return;
    }
    
    writeFile.write(doc.toJson(QJsonDocument::Indented));
    writeFile.close();
    
    // m_lib->m_Log->writeLog(4, QString("Configuration successfully saved to: ") + settingsPath);
}

void Settings::loadIODevConfig()
{
    QList<s_IODevices> loadedDevices;
    int recordDevId, playbackDevId;

    // Load JSON configuration
    QJsonObject config = loadJsonConfig();
    
    // Check if config is empty
    if (config.isEmpty()) {
        m_lib->m_Log->writeLog(1, QString("loadIODevConfig: No configuration loaded"));
        return;
    }
    
    // Try to get ioDevices array (used in test1.json format)
    QJsonArray deviceArray = config["ioDevices"].toArray();
    
    // If ioDevices doesn't exist, try audio_devices as fallback
    if (deviceArray.isEmpty()) {
        deviceArray = config["audio_devices"].toArray();
    }
    
    m_lib->m_Log->writeLog(1, QString("loadIODevConfig: Found %1 devices in configuration").arg(deviceArray.size()));
    
    // Convert JSON array to QList<s_IODevices>
    for (const QJsonValue &value : deviceArray) {
        QJsonObject obj = value.toObject();
        
        s_IODevices device;
        
        // Handle different JSON formats
        if (obj.contains("devicetype")) {
            device.devicetype = static_cast<DeviceType>(obj["devicetype"].toInt());
            
            // Direct string values (test1.json format)
            if (obj.contains("inputname")) {
                if (obj["inputname"].isString()) {
                    device.inputname = obj["inputname"].toString();
                } else if (obj["inputname"].isObject()) {
                    device.inputname = obj["inputname"].toObject()["value"].toString();
                }
            }
            
            if (obj.contains("outputname")) {
                if (obj["outputname"].isString()) {
                    device.outputame = obj["outputname"].toString();
                } else if (obj["outputname"].isObject()) {
                    device.outputame = obj["outputname"].toObject()["value"].toString();
                }
            }
            
            if (obj.contains("path")) {
                if (obj["path"].isString()) {
                    device.path = obj["path"].toString();
                } else if (obj["path"].isObject()) {
                    device.path = obj["path"].toObject()["value"].toString();
                }
            }
            
            if (obj.contains("genfrequency")) {
                if (obj["genfrequency"].isDouble()) {
                    device.genfrequency = obj["genfrequency"].toInt();
                } else if (obj["genfrequency"].isObject()) {
                    device.genfrequency = obj["genfrequency"].toObject()["value"].toInt();
                }
            }
            
            // Generate UID if not present
            if (obj.contains("uid")) {
                device.uid = obj["uid"].toString();
            } else {
                device.uid = QString("%1_%2").arg(device.inputname).arg(device.outputame);
            }
            
            loadedDevices.append(device);
            m_lib->m_Log->writeLog(5, QString("loadIODevConfig: Loaded device: %1 -> %2 (type: %3)")
                .arg(device.inputname)
                .arg(device.outputame)
                .arg(device.devicetype));
        }
    }


    QString MasterClockDev = getMasterClock();

    bool clockdevFound = false;
    for( int i=0; i<loadedDevices.count(); ++i ){
        if(loadedDevices.at(i).devicetype == SoundDevice){                                      // add the Clocking Device first because the other devices get the clock from the port 0 of the conference bridge.
            if(loadedDevices.at(i).inputname == MasterClockDev){
                recordDevId = m_lib->m_AudioRouter->getSoundDevID(loadedDevices.at(i).inputname);
                playbackDevId = m_lib->m_AudioRouter->getSoundDevID(loadedDevices.at(i).outputame);
                m_lib->m_AudioRouter->AddClockingDevice(recordDevId,playbackDevId, loadedDevices.at(i).uid);
                // m_lib->m_Log->writeLog(3,QString("loadIODevConfig: added Master clocking sound device form config file: ") + loadedDevices.at(i).inputname + " " + loadedDevices.at(i).outputame);
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
                m_lib->m_Log->writeLog(4,QString("loadIODevConfig: added sound device from config file: ") + loadedDevices.at(i).inputname + " " + loadedDevices.at(i).outputame);
            }
            else{
                m_lib->m_AudioRouter->setAudioDeviceToOffline(loadedDevices.at(i).inputname,loadedDevices.at(i).outputame, loadedDevices.at(i).uid);
                m_lib->m_Log->writeLog(1,QString("loadIODevConfig: Error loading sound Device: ") + loadedDevices.at(i).inputname + loadedDevices.at(i).outputame + " device not found");
            }
        }
        if(loadedDevices.at(i).devicetype == TestToneGenerator){
            m_lib->m_AudioRouter->addToneGen(loadedDevices.at(i).genfrequency, loadedDevices.at(i).uid);
            m_lib->m_Log->writeLog(4,QString("loadIODevConfig: added Generator from config file: ") + loadedDevices.at(i).inputname);

        }
        if(loadedDevices.at(i).devicetype == FilePlayer){
            m_lib->m_AudioRouter->addFilePlayer(loadedDevices.at(i).inputname, loadedDevices.at(i).path, loadedDevices.at(i).uid);
            m_lib->m_Log->writeLog(4,QString("loadIODevConfig: added FilePlayer from config file: ") + loadedDevices.at(i).inputname);
        }
        if(loadedDevices.at(i).devicetype == FileRecorder){
            m_lib->m_AudioRouter->addFileRecorder(loadedDevices.at(i).path, loadedDevices.at(i).uid);
            m_lib->m_Log->writeLog(4,QString("loadIODevConfig: added FileRecorder from config file: ") + loadedDevices.at(i).outputame);
        }
        if(loadedDevices.at(i).devicetype == LatencyMonitor){
            m_lib->m_AudioRouter->addLatencyMonitor(loadedDevices.at(i).uid);
            m_lib->m_Log->writeLog(4,QString("loadIODevConfig: added LatencyMonitor from config file"));
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
    
    config["ioDevices"] = deviceArray;
    
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
    
    // GPIO devices are in the same ioDevices array as audio devices
    // We need to filter for GPIO device types (typically devicetype >= 4)
    QJsonArray deviceArray = config["ioDevices"].toArray();
    
    // If ioDevices doesn't exist, try GpioDevConfig as fallback
    if (deviceArray.isEmpty()) {
        deviceArray = config["GpioDevConfig"].toArray();
    }

    m_lib->m_Log->writeLog(5, QString("loadIODevConfigLater: Found %1 devices total, filtering for GPIO devices").arg(deviceArray.size()));

    // Convert JSON array to QList<s_IODevices>, filtering for GPIO devices
    for (const QJsonValue &value : deviceArray) {
        QJsonObject obj = value.toObject();
        if (obj.contains("uid") && obj.contains("devicetype")) {
            int deviceType = obj["devicetype"].toInt();
            
            // Filter for GPIO device types based on DeviceType enum:
            // VirtualGpioDevice(4), LogicAndGpioDevice(5), LogicOrGpioDevice(6), 
            // AccountGpioDevice(7), LinuxGpioDevice(8), AudioCrosspointDevice(9)
            if (deviceType >= VirtualGpioDevice && deviceType <= AudioCrosspointDevice) {
                // Get device type name for logging
                QString deviceTypeName;
                switch(static_cast<DeviceType>(deviceType)) {
                    case VirtualGpioDevice: deviceTypeName = "VirtualGpioDevice"; break;
                    case LogicAndGpioDevice: deviceTypeName = "LogicAndGpioDevice"; break;
                    case LogicOrGpioDevice: deviceTypeName = "LogicOrGpioDevice"; break;
                    case AccountGpioDevice: deviceTypeName = "AccountGpioDevice"; break;
                    case LinuxGpioDevice: deviceTypeName = "LinuxGpioDevice"; break;
                    case AudioCrosspointDevice: deviceTypeName = "AudioCrosspointDevice"; break;
                    default: deviceTypeName = QString("Unknown GPIO Type (%1)").arg(deviceType); break;
                }
                
                s_IODevices device;
                device.uid = obj["uid"].toString();
                device.devicetype = static_cast<DeviceType>(deviceType);
                device.inputname = obj.contains("inputname") ? obj["inputname"].toString() : QString();
                device.outputame = obj.contains("outputname") ? obj["outputname"].toString() : QString();
                device.path = obj.contains("path") ? obj["path"].toString() : QString();
                device.genfrequency = obj.contains("genfrequency") ? obj["genfrequency"].toInt() : 0;
                
                // Load device IDs (test1.json compatibility)
                device.PBDevID = obj.contains("PBDevID") ? obj["PBDevID"].toInt() : 0;
                device.RecDevID = obj.contains("RecDevID") ? obj["RecDevID"].toInt() : 0;
                
                // Load typeSpecificSettings if present (test1.json compatibility)
                if (obj.contains("typeSpecificSettings")) {
                    device.typeSpecificSettings["typeSpecificSettings"] = obj["typeSpecificSettings"];
                }
                
                // IMPORTANT: Load channel counts for GPIO devices
                device.inChannelCount = obj.contains("inChannelCount") ? obj["inChannelCount"].toInt() : 0;
                device.outChannelCount = obj.contains("outChannelCount") ? obj["outChannelCount"].toInt() : 0;
                

                // Log channel configuration for info
                if (device.inChannelCount == 0 && device.outChannelCount > 0) {
                    m_lib->m_Log->writeLog(5,QString("loadGpioDevManager: %1 configured as output-only device")
                        .arg(deviceTypeName));
                } else if (device.inChannelCount > 0 && device.outChannelCount == 0) {
                    m_lib->m_Log->writeLog(5,QString("loadGpioDevManager: %1 configured as input-only device")
                        .arg(deviceTypeName));
                }
                
                loadedDevices.append(device);
                
                GpioDevice* createdDevice = m_lib->m_GpioDeviceManager->createGeneric(device);
                
                if(createdDevice) {
                    m_lib->m_Log->writeLog(4,QString("loadGpioDevManager: Loaded %1 device: %2")
                        .arg(deviceTypeName)
                        .arg(device.outputame.isEmpty() ? device.inputname : device.outputame));
                } else {
                    m_lib->m_Log->writeLog(2,QString("loadGpioDevManager: ERROR - Failed to create %1 device: %2")
                        .arg(deviceTypeName)
                        .arg(device.uid));
                }
            }
            // Also allow LatencyMonitor to persist in ioDevices
            if (deviceType == LatencyMonitor) {
                s_IODevices device;
                device.fromJSON(obj);
                loadedDevices.append(device);
                m_lib->m_Log->writeLog(5, QString("loadIODevConfigLater: Loaded LatencyMonitor device"));
            }
        }
    }
    
    // m_lib->m_Log->writeLog(3, QString("loadIODevConfigLater: Successfully loaded %1 GPIO devices").arg(loadedDevices.size()));
    m_GpioDevicesLoaded = true;
    loadGpioRoutes();
}

void Settings::saveGpioDevConfig()
{
    if (!m_GpioDevicesLoaded)
        return;
        
    // Load existing configuration
    QJsonObject config = loadJsonConfig();
    
    // Get existing ioDevices array to preserve non-GPIO devices
    QJsonArray existingDevices;
    if (config.contains("ioDevices")) {
        existingDevices = config["ioDevices"].toArray();
    }
    
    // Filter out old GPIO devices from ioDevices array
    QJsonArray newDeviceArray;
    for (const QJsonValue &value : existingDevices) {
        QJsonObject obj = value.toObject();
        if (obj.contains("devicetype")) {
            int deviceType = obj["devicetype"].toInt();
            // Keep only non-GPIO devices (0-3: Sound, TestTone, FilePlayer, FileRecorder)
            if (deviceType < VirtualGpioDevice || deviceType > AudioCrosspointDevice) {
                newDeviceArray.append(value);
            }
        }
    }
    
    // Add current GPIO devices to the ioDevices array
    const QList<s_IODevices>& gpioDevices = m_lib->m_GpioDeviceManager->getGpioDevices();
    for (const s_IODevices &device : gpioDevices) {
        QJsonObject obj;
        

        obj["uid"] = device.uid;
        obj["devicetype"] = static_cast<int>(device.devicetype);
        obj["inputname"] = device.inputname;
        obj["outputname"] = device.outputame;
        obj["path"] = device.path;
        obj["genfrequency"] = device.genfrequency;
        
        // Channel counts for GPIO devices
        obj["inChannelCount"] = static_cast<int>(device.inChannelCount);
        obj["outChannelCount"] = static_cast<int>(device.outChannelCount);
        

        obj["PBDevID"] = device.PBDevID;
        obj["RecDevID"] = device.RecDevID;
        
        // Additional fields for full test1.json compatibility
        obj["portNo"] = QJsonArray(); // Empty array like in test1.json
        
        // typeSpecificSettings (preserve existing or create empty)
        QJsonObject typeSettings;
        if (device.typeSpecificSettings.contains("typeSpecificSettings")) {
            typeSettings = device.typeSpecificSettings["typeSpecificSettings"].toObject();
        }
        obj["typeSpecificSettings"] = typeSettings;
        
        newDeviceArray.append(obj);
        
    }
    
    // Update ioDevices array with merged devices (audio + GPIO)
    config["ioDevices"] = newDeviceArray;
    
    // Save updated configuration
    saveJsonConfig(config);
    
    // m_lib->m_Log->writeLog(3, QString("saveGpioDevConfig: Successfully saved %1 GPIO devices to ioDevices array").arg(gpioDevices.size()));
}

void Settings::loadGpioRoutes()
{
    QJsonObject config = loadJsonConfig();
    
    // Try both "gpioRoutes" and "GpioRoutes" for flexibility
    QJsonArray routeArray;
    if (config.contains("gpioRoutes")) {
        routeArray = config["gpioRoutes"].toArray();
        // m_lib->m_Log->writeLog(5, QString("loadGpioRoutes: found gpioRoutes array"));
    } else if (config.contains("GpioRoutes")) {
        routeArray = config["GpioRoutes"].toArray();
        // m_lib->m_Log->writeLog(5, QString("loadGpioRoutes: found GpioRoutes array"));
    } else {
        m_lib->m_Log->writeLog(4, QString("loadGpioRoutes: no GPIO routes found in config"));
    }

    // m_lib->m_Log->writeLog(3, QString("loadGpioRoutes: loaded %1 routes").arg(routeArray.size()));

    for (const QJsonValue &value : routeArray) {
        QJsonObject obj = value.toObject();
        if (obj.contains("srcSlotId") && obj.contains("destSlotId")) {
            QString srcSlotId = obj["srcSlotId"].toString();
            QString destSlotId = obj["destSlotId"].toString();
            bool inverted = obj["inverted"].toBool();
            bool persistant = obj["persistant"].toBool();
            
            // m_lib->m_Log->writeLog(5, QString("loadGpioRoutes: loading route from %1 to %2").arg(srcSlotId, destSlotId));
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
            obj["srcSlotId"] = route.srcSlotId;
            obj["destSlotId"] = route.destSlotId;
            obj["inverted"] = route.inverted;
            obj["persistant"] = route.persistant;
            routeArray.append(obj);
            // m_lib->m_Log->writeLog(5, QString("saveGpioRoutes: Saving route %1 -> %2").arg(route.srcSlotId, route.destSlotId));
        }
    }
    
    // Use lowercase "gpioRoutes" to match test1.json format
    config["gpioRoutes"] = routeArray;
    saveJsonConfig(config);
    m_lib->m_Log->writeLog(1, QString("saveGpioRoutes: Successfully saved %1 GPIO routes").arg(routeArray.size()));
}

void Settings::loadBuddies()
{
    QList<s_buddy> loadedBuddies;
    QJsonObject config = loadJsonConfig();
    
    // Check if config is empty
    if (config.isEmpty()) {
        m_lib->m_Log->writeLog(1, QString("loadBuddies: No configuration loaded"));
        return;
    }
    
    // Try to get buddies array (test1.json format uses lowercase)
    QJsonArray buddyArray = config["buddies"].toArray();
    
    // If buddies doesn't exist, try Buddies as fallback
    if (buddyArray.isEmpty()) {
        buddyArray = config["Buddies"].toArray();
    }
    
    m_lib->m_Log->writeLog(1, QString("loadBuddies: Found %1 buddies in configuration").arg(buddyArray.size()));

    for (const QJsonValue &value : buddyArray) {
        QJsonObject obj = value.toObject();
        
        // Helper function to load string fields - supports both direct values and nested objects
        auto loadStringField = [&](const QString &field, const QString &defaultValue = QString()) -> QString {
            if (!obj.contains(field)) {
                return defaultValue;
            }
            
            QJsonValue val = obj[field];
            if (val.isString()) {
                // Direct string value (test1.json format)
                QString result = val.toString();
                // m_lib->m_Log->writeLog(5, QString("Loaded buddy %1: %2 (direct)").arg(field).arg(result));
                return result;
            } else if (val.isObject()) {
                // Nested object with "value" field (saveBuddies format)
                QJsonObject fieldObj = val.toObject();
                if (fieldObj.contains("value")) {
                    QString result = fieldObj["value"].toString();
                    // m_lib->m_Log->writeLog(5, QString("Loaded buddy %1: %2 (nested)").arg(field).arg(result));
                    return result;
                }
            }
            return defaultValue;
        };
        
        // Check if this entry has the required fields
        if (obj.contains("Name") || obj.contains("buddyUrl") || obj.contains("uid")) {
            s_buddy buddy;
            
            // Load basic buddy information
            buddy.uid = loadStringField("uid");
            buddy.buddyUrl = loadStringField("buddyUrl");  
            buddy.Name = loadStringField("Name");
            buddy.accUid = loadStringField("accUid");
            
            // Generate UID if not present
            if (buddy.uid.isEmpty()) {
                buddy.uid = QString("%1_%2").arg(buddy.Name).arg(buddy.buddyUrl);
            }
            
            // Load codec information if present
            if (obj.contains("codec")) {
                s_codec codec;
                QJsonValue codecVal = obj["codec"];
                
                if (codecVal.isObject()) {
                    // Direct codec object (test1.json format)
                    codec.fromJSON(codecVal.toObject());
                } else if (codecVal.isObject()) {
                    // Check if it's nested format
                    QJsonObject codecObj = codecVal.toObject();
                    if (codecObj.contains("value") && codecObj["value"].isObject()) {
                        codec.fromJSON(codecObj["value"].toObject());
                    } else {
                        codec.fromJSON(codecObj);
                    }
                }
                buddy.codec = codec;
            }
            
            // Add the buddy to the system
            m_lib->m_Buddies->addBuddy(buddy.buddyUrl, buddy.Name, buddy.accUid, buddy.codec.toJSON(), buddy.uid);
            
            m_lib->m_Log->writeLog(1, QString("loadBuddies: Successfully loaded buddy: %1 (%2) for account %3")
                .arg(buddy.Name)
                .arg(buddy.buddyUrl)
                .arg(buddy.accUid));
            
            loadedBuddies.append(buddy);
        }
    }
    
    m_lib->m_Log->writeLog(1, QString("loadBuddies: Successfully loaded %1 buddies").arg(loadedBuddies.size()));
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
        
        // Use test1.json format (direct values, lowercase key)
        obj["uid"] = buddy.uid;
        obj["buddyUrl"] = buddy.buddyUrl;
        obj["Name"] = buddy.Name;
        obj["accUid"] = buddy.accUid;
        obj["codec"] = buddy.codec.toJSON();
        
        buddyArray.append(obj);
    }
    
    // Use lowercase "buddies" to match test1.json format
    config["buddies"] = buddyArray;
    saveJsonConfig(config);
}

void Settings::loadAccConfig()
{
    QJsonObject config = loadJsonConfig();
    
    // Check if config is empty
    if (config.isEmpty()) {
        m_lib->m_Log->writeLog(1, QString("loadAccConfig: No configuration loaded"));
        return;
    }
    
    QJsonArray accountArray = config["accounts"].toArray();
    m_lib->m_Log->writeLog(1, QString("loadAccConfig: Found %1 accounts in configuration").arg(accountArray.size()));

    for (const QJsonValue &value : accountArray) {
        QJsonObject obj = value.toObject();
        
        if (obj.contains("AccID")) {
            s_account acc;
            acc.uid = obj["AccID"].toString();
            
            // Helper function to load string fields - supports both direct values and nested objects
            auto loadStringField = [&](const QString &field, const QString &defaultValue = QString()) -> QString {
                if (!obj.contains(field)) {
                    return defaultValue;
                }
                
                QJsonValue val = obj[field];
                if (val.isString()) {
                    // Direct string value (test1.json format)
                    QString result = val.toString();
                    // Account detail logging removed to reduce spam
                    // m_lib->m_Log->writeLog(5, QString("Loaded %1: %2 (direct)").arg(field).arg(result));
                    return result;
                } else if (val.isObject()) {
                    // Nested object with "value" field (saveAccConfig format)
                    QJsonObject fieldObj = val.toObject();
                    if (fieldObj.contains("value")) {
                        QString result = fieldObj["value"].toString();
                        // Account detail logging removed to reduce spam
                        // m_lib->m_Log->writeLog(5, QString("Loaded %1: %2 (nested)").arg(field).arg(result));
                        return result;
                    }
                }
                m_lib->m_Log->writeLog(3, QString("Could not load field %1").arg(field));
                return defaultValue;
            };
            
            // Helper function to load boolean fields
            auto loadBoolField = [&](const QString &field, bool defaultValue = false) -> bool {
                if (!obj.contains(field)) {
                    return defaultValue;
                }
                
                QJsonValue val = obj[field];
                if (val.isBool()) {
                    // Direct boolean value (test1.json format)
                    bool result = val.toBool();
                    // Account detail logging removed to reduce spam
                    // m_lib->m_Log->writeLog(6, QString("Loaded %1: %2 (direct bool)").arg(field).arg(result));
                    return result;
                } else if (val.isObject()) {
                    // Nested object with "value" field
                    QJsonObject fieldObj = val.toObject();
                    if (fieldObj.contains("value")) {
                        bool result = fieldObj["value"].toBool();
                        // Account detail logging removed to reduce spam
                        // m_lib->m_Log->writeLog(3, QString("Loaded %1: %2 (nested bool)").arg(field).arg(result));
                        return result;
                    }
                }
                return defaultValue;
            };
            
            // Helper function to load integer fields
            auto loadIntField = [&](const QString &field, int defaultValue = 0) -> int {
                if (!obj.contains(field)) {
                    return defaultValue;
                }
                
                QJsonValue val = obj[field];
                if (val.isDouble()) {
                    // Direct integer value (test1.json format)
                    int result = val.toInt();
                    // Account detail logging removed to reduce spam
                    // m_lib->m_Log->writeLog(6, QString("Loaded %1: %2 (direct int)").arg(field).arg(result));
                    return result;
                } else if (val.isObject()) {
                    // Nested object with "value" field
                    QJsonObject fieldObj = val.toObject();
                    if (fieldObj.contains("value")) {
                        int result = fieldObj["value"].toInt();
                        // Account detail logging removed to reduce spam
                        // m_lib->m_Log->writeLog(3, QString("Loaded %1: %2 (nested int)").arg(field).arg(result));
                        return result;
                    }
                }
                return defaultValue;
            };
            
            // Load all account fields
            acc.name = loadStringField("name");
            acc.serverURI = loadStringField("serverURI");
            acc.user = loadStringField("user");
            acc.password = loadStringField("password");
            acc.FilePlayPath = loadStringField("FilePlayPath");
            acc.FileRecordPath = loadStringField("FileRecordPath");
            
            acc.FileRecordRXonly = loadBoolField("FileRecordRXonly");
            acc.fixedJitterBuffer = loadBoolField("fixedJitterBuffer");
            acc.fixedJitterBufferValue = loadIntField("fixedJitterBufferValue");
            acc.autoconnectToBuddyUID = loadStringField("autoconnectToBuddyUID");
            acc.autoconnectEnable = loadBoolField("autoconnectEnable");
            acc.hasDTMFGPIO = loadBoolField("hasDTMFGPIO");
            
            acc.hasDTMFGPIO = loadBoolField("hasDTMFGPIO");
            
            // Load CallHistory Array
            if (obj.contains("CallHistory")) {
                QJsonArray historyArray = obj["CallHistory"].toArray();
                // Account detail logging reduced to prevent spam
                // m_lib->m_Log->writeLog(5, QString("Loading %1 call history entries").arg(historyArray.size()));
                
                for (const QJsonValue &histVal : historyArray) {
                    if (histVal.isObject()) {
                        QJsonObject histObj = histVal.toObject();
                        s_callHistory callHist;
                        
                        callHist.callUri = histObj["callUri"].toString();
                        
                        // Load additional fields if present
                        if (histObj.contains("count")) {
                            callHist.count = histObj["count"].toInt();
                        }
                        if (histObj.contains("duration")) {
                            callHist.duration = histObj["duration"].toInt();
                        }
                        if (histObj.contains("outgoing")) {
                            callHist.outgoing = histObj["outgoing"].toBool();
                        }
                        
                        // Load codec information if present
                        if (histObj.contains("codec")) {
                            s_codec codec;
                            codec.fromJSON(histObj["codec"].toObject());
                            callHist.codec = codec;
                        }
                        
                        acc.CallHistory.append(callHist);
                    }
                }
            }
            
            // Create the account
            m_lib->m_Accounts->createAccount(acc.name, acc.serverURI, acc.user, acc.password, 
                                           acc.FilePlayPath, acc.FileRecordPath, acc.FileRecordRXonly,
                                           acc.fixedJitterBuffer, acc.fixedJitterBufferValue,
                                           acc.autoconnectToBuddyUID, acc.autoconnectEnable,
                                           acc.hasDTMFGPIO, acc.CallHistory, acc.uid);
                                           
            // m_lib->m_Log->writeLog(3, QString("loadAccConfig: Successfully loaded account: %1 (user: %2, server: %3)")
            //     .arg(acc.name)
            //     .arg(acc.user) 
            //     .arg(acc.serverURI));
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
        obj["AccID"] = acc.uid;

        if (!acc.name.isEmpty()) {
            QJsonObject nameObj;
            nameObj["type"] = 1;
            nameObj["value"] = acc.name;
            obj["name"] = nameObj;
        }

        if (!acc.serverURI.isEmpty()) {
            QJsonObject serverObj;
            serverObj["type"] = 1;
            serverObj["value"] = acc.serverURI;
            obj["serverURI"] = serverObj;
        }

        if (!acc.user.isEmpty()) {
            QJsonObject userObj;
            userObj["type"] = 1;
            userObj["value"] = acc.user;
            obj["user"] = userObj;
        }

        if (!acc.password.isEmpty()) {
            QJsonObject pwdObj;
            pwdObj["type"] = 1;
            pwdObj["value"] = acc.password;
            obj["password"] = pwdObj;
        }

        // FilePlayPath als String
        QJsonObject playPathObj;
        playPathObj["type"] = 1; // String type
        playPathObj["value"] = acc.FilePlayPath;
        obj["FilePlayPath"] = playPathObj;

        // FileRecordPath als String
        QJsonObject recordPathObj;
        recordPathObj["type"] = 1; // String type
        recordPathObj["value"] = acc.FileRecordPath;
        obj["FileRecordPath"] = recordPathObj;

        // FileRecordRXonly als Boolean
        QJsonObject rxOnlyObj;
        rxOnlyObj["type"] = 3; // Boolean type
        rxOnlyObj["value"] = acc.FileRecordRXonly;
        obj["FileRecordRXonly"] = rxOnlyObj;

        // fixedJitterBuffer als Boolean
        QJsonObject jitterObj;
        jitterObj["type"] = 3; // Boolean type
        jitterObj["value"] = acc.fixedJitterBuffer;
        obj["fixedJitterBuffer"] = jitterObj;

        // fixedJitterBufferValue als Integer
        QJsonObject jitterValObj;
        jitterValObj["type"] = 2; // Integer type
        jitterValObj["value"] = static_cast<int>(acc.fixedJitterBufferValue);
        obj["fixedJitterBufferValue"] = jitterValObj;

        // autoconnectToBuddyUID als String
        QJsonObject autoconnectUidObj;
        autoconnectUidObj["type"] = 1; // String type
        autoconnectUidObj["value"] = acc.autoconnectToBuddyUID;
        obj["autoconnectToBuddyUID"] = autoconnectUidObj;

        // autoconnectEnable als Boolean
        QJsonObject autoconnectObj;
        autoconnectObj["type"] = 3; // Boolean type
        autoconnectObj["value"] = acc.autoconnectEnable;
        obj["autoconnectEnable"] = autoconnectObj;

        // hasDTMFGPIO als Boolean
        QJsonObject dtmfObj;
        dtmfObj["type"] = 3; // Boolean type
        dtmfObj["value"] = acc.hasDTMFGPIO;
        obj["hasDTMFGPIO"] = dtmfObj;
        
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
    
    config["accounts"] = accountArray;
    saveJsonConfig(config);
}

void Settings::loadWebRTCChannelConfig() {
    QJsonObject config = loadJsonConfig();
    QJsonArray chanArray = config["WebRTCChannels"].toArray();
    int added = 0;
    for (const QJsonValue &value : chanArray) {
        QJsonObject obj = value.toObject();
        const QString id = obj["id"].toString();
        if (id.isEmpty()) continue;
        const QString desc = obj["description"].toString();
        const bool enabled = obj["enabled"].toBool(true);
        m_lib->m_WebRTCChannels->createChannel(id, desc, enabled);
        // Populate optional fields
        if (s_webrtc_channel* ch = m_lib->m_WebRTCChannels->getChannelById(id)) {
            // Direction and limits
            if (obj.contains("sendOnly")) {
                m_lib->m_WebRTCChannels->setChannelSendOnly(id, obj["sendOnly"].toBool(false));
            }
            if (obj.contains("maxConcurrentStreams")) {
                m_lib->m_WebRTCChannels->setChannelMaxCalls(id, obj["maxConcurrentStreams"].toInt(ch->maxConcurrentStreams));
            }
            // STUN/TURN
            if (obj.contains("stunServer")) {
                m_lib->m_WebRTCChannels->setChannelStunServer(id, obj["stunServer"].toString(ch->stunServer));
            }
            if (obj.contains("turnServer") || obj.contains("turnUsername") || obj.contains("turnCredential")) {
                const QString turnSrv = obj["turnServer"].toString(ch->turnServer);
                const QString turnUser = obj["turnUsername"].toString(ch->turnUsername);
                const QString turnCred = obj["turnCredential"].toString(ch->turnCredential);
                m_lib->m_WebRTCChannels->setChannelTurnServer(id, turnSrv, turnUser, turnCred);
            }
            // ICE flags
            if (obj.contains("iceEnabled")) ch->iceEnabled = obj["iceEnabled"].toBool(ch->iceEnabled);
            if (obj.contains("turnEnabled")) ch->turnEnabled = obj["turnEnabled"].toBool(ch->turnEnabled);
            if (obj.contains("trickleIceEnabled")) ch->trickleIceEnabled = obj["trickleIceEnabled"].toBool(ch->trickleIceEnabled);
            if (obj.contains("iceAlwaysUpdate")) ch->iceAlwaysUpdate = obj["iceAlwaysUpdate"].toBool(ch->iceAlwaysUpdate);
        }
        ++added;
    }
    m_lib->m_Log->writeLog(3, QString("loadWebRTCChannelConfig: loaded channels: ") + QString::number(added));
    m_WebRTCChannelsLoaded = true;
    // Ensure persisted config includes optional fields post-load
    m_lib->m_Settings->saveWebRTCChannelConfig();
}

void Settings::saveWebRTCChannelConfig() {
    if (!m_WebRTCChannelsLoaded)
        return;
    QJsonObject config = loadJsonConfig();
    QJsonArray chanArray;
    const QList<s_webrtc_channel>* chans = m_lib->m_WebRTCChannels->getChannels();
    for (const s_webrtc_channel &ch : *chans) {
        QJsonObject obj;
        obj["id"] = ch.id;
        obj["description"] = ch.description;
        obj["enabled"] = ch.enabled;
        // Optional fields (persist if present)
        obj["sendOnly"] = ch.sendOnly;
        obj["maxConcurrentStreams"] = ch.maxConcurrentStreams;
        obj["stunServer"] = ch.stunServer;
        obj["turnServer"] = ch.turnServer;
        obj["turnUsername"] = ch.turnUsername;
        obj["turnCredential"] = ch.turnCredential;
        obj["iceEnabled"] = ch.iceEnabled;
        obj["turnEnabled"] = ch.turnEnabled;
        obj["trickleIceEnabled"] = ch.trickleIceEnabled;
        obj["iceAlwaysUpdate"] = ch.iceAlwaysUpdate;
        chanArray.append(obj);
    }
    config["WebRTCChannels"] = chanArray;
    saveJsonConfig(config);
}

int Settings::loadAudioRoutes()
{
    int status = PJ_SUCCESS;
    QList<s_audioRoutes> loadedRoutes;
    // Ensure maps are populated at startup
    m_lib->m_AudioRouter->refreshConfPortMaps();
    QMap<int, QString> srcAudioSlotMap = m_lib->m_AudioRouter->getSrcAudioSlotMap();
    QMap<int, QString> destAudioSlotMap = m_lib->m_AudioRouter->getDestAudioSlotMap();
    
    QJsonObject config = loadJsonConfig();
    
    // Check if config is empty
    if (config.isEmpty()) {
        m_lib->m_Log->writeLog(1, QString("loadAudioRoutes: No configuration loaded"));
        return PJ_SUCCESS;
    }
    
    // Try to get audioRoutes array (test1.json format uses lowercase)
    QJsonArray routeArray = config["audioRoutes"].toArray();
    
    // If audioRoutes doesn't exist, try AudioRoutes as fallback  
    if (routeArray.isEmpty()) {
        routeArray = config["AudioRoutes"].toArray();
    }
    
    m_lib->m_Log->writeLog(1, QString("loadAudioRoutes: Found %1 routes in configuration").arg(routeArray.size()));
    
    m_lib->m_AudioRouter->clearAllOfflineAudioRoutes();
    
    // Konvertiere JSON Array zu QList<s_audioRoutes> (supports legacy and parent routes)
    for (const QJsonValue &value : routeArray) {
        QJsonObject obj = value.toObject();
        s_audioRoutes route;
        // Legacy/dev name entries
        if (obj.contains("srcDevName")) route.srcDevName = obj["srcDevName"].toString();
        if (obj.contains("destDevName")) route.destDevName = obj["destDevName"].toString();
        // Extended parent entries
        route.srcIsParent = obj["srcIsParent"].toBool(false);
        route.destIsParent = obj["destIsParent"].toBool(false);
        route.srcParentKey = obj["srcParentKey"].toString();
        route.destParentKey = obj["destParentKey"].toString();
        route.srcParentChannel = obj["srcParentChannel"].toInt(0);
        route.destParentChannel = obj["destParentChannel"].toInt(0);
        route.level = obj["level"].toVariant().toInt();
        route.persistant = obj["persistant"].toBool();
        // Skip empty rows
        if (!route.srcIsParent && !route.destIsParent && route.srcDevName.isEmpty() && route.destDevName.isEmpty()) {
            continue;
        }
        loadedRoutes.append(route);
    }
    
    m_lib->m_Log->writeLog(3, QString("loadAudioRoutes: loaded routes: ") + QString::number(loadedRoutes.count()));
    for(auto& route : loadedRoutes ){
        // Parent routes: store intent via AudioRouter unified API (ensure negative slots are set for GUI)
        if (route.srcIsParent || route.destIsParent) {
            // Use router API to reconstruct parent routes honoring channel
            if (route.srcIsParent && !route.srcParentKey.isEmpty()) {
                QString other = route.destIsParent ? route.destParentKey : route.destDevName;
                m_lib->m_AudioRouter->connectParentToDev(route.srcParentKey, Entity_Unknown, true, other, route.level, route.persistant, route.srcParentChannel, route.destParentChannel);
            } else if (route.destIsParent && !route.destParentKey.isEmpty()) {
                QString other = route.srcIsParent ? route.srcParentKey : route.srcDevName;
                m_lib->m_AudioRouter->connectParentToDev(route.destParentKey, Entity_Unknown, false, other, route.level, route.persistant, route.destParentChannel, route.srcParentChannel);
            }
            continue;
        }
        // Legacy direct routes: try to map now, otherwise store as offline until devices appear
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
            else {
                // Individual connection logging removed to reduce spam
                // m_lib->m_Log->writeLog(3,QString("loadAudioRoutes: Successfully connected: ") + route.srcDevName + " to " + route.destDevName);
            }
        } else {
            m_lib->m_AudioRouter->addOfflineAudioRoute(route);
            m_lib->m_Log->writeLog(2, QString("loadAudioRoutes: Route offline (devices not found): %1 -> %2")
                .arg(route.srcDevName).arg(route.destDevName));
            status = -1;
        }
    }
    m_AudioRoutesLoaded = true;
    saveAudioRoutes();
    // Ensure UI sees loaded routes immediately
    if (m_lib && m_lib->m_AudioRouter) {
        m_lib->m_AudioRouter->scheduleConferenceRefresh(0);
    }
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
    
    // Sammle persistente Routes und prüfe auf Duplikate (consider parent keys and channels)
    auto makeKey = [](const s_audioRoutes &r)->QString{
        // Include channels so Ch:1 and Ch:2 routes are distinct
        return QString("%1|%2|%3|%4|%5|%6|srcCh:%7|dstCh:%8")
            .arg(r.srcIsParent)
            .arg(r.srcParentKey, r.srcDevName, r.destParentKey, r.destDevName)
            .arg(r.destIsParent)
            .arg(r.srcParentChannel)
            .arg(r.destParentChannel);
    };
    QSet<QString> seen;
    for(const auto& route : audioRoutes) {
        if(route.persistant) {
            const QString key = makeKey(route);
            if (seen.contains(key)) continue;
            seen.insert(key);
            routesToSave.append(route);
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
    
    // Konvertiere zu JSON (write new optional fields when present)
    for(const auto& route : routesToSave) {
        QJsonObject obj;
        if (!route.srcDevName.isEmpty()) obj["srcDevName"] = route.srcDevName;
        if (!route.destDevName.isEmpty()) obj["destDevName"] = route.destDevName;
        if (route.srcIsParent) { obj["srcIsParent"] = true; if (!route.srcParentKey.isEmpty()) obj["srcParentKey"] = route.srcParentKey; if (route.srcParentChannel>0) obj["srcParentChannel"]=route.srcParentChannel; }
        if (route.destIsParent) { obj["destIsParent"] = true; if (!route.destParentKey.isEmpty()) obj["destParentKey"] = route.destParentKey; if (route.destParentChannel>0) obj["destParentChannel"]=route.destParentChannel; }
        obj["level"] = route.level;
        obj["persistant"] = route.persistant;
        routeArray.append(obj);
    }
    
    // Use lowercase "audioRoutes" to match test1.json format
    config["audioRoutes"] = routeArray;
    saveJsonConfig(config);
    return PJ_SUCCESS;
}

void Settings::saveCustomSourceNames()
{
    QJsonObject config = loadJsonConfig();
    QJsonObject audioSettings;
    if (config.contains("AudioSettings")) {
        audioSettings = config["AudioSettings"].toObject();
    }
    
    QMap<QString,QString> srcLabels = m_lib->m_AudioRouter->getCustomSourceLabels();
    for (auto i = srcLabels.constBegin(); i != srcLabels.constEnd(); ++i) {
        QJsonObject labelObj;
        labelObj["type"] = 1;  // String type wie in test1.json
        labelObj["value"] = i.value();
        audioSettings[QString("CustomSource_%1").arg(i.key())] = labelObj;
    }
    
    config["AudioSettings"] = audioSettings;
    saveJsonConfig(config);
    
    // m_lib->m_Log->writeLog(3, QString("saveCustomSourceNames: saved %1 custom source labels").arg(srcLabels.size()));
}

void Settings::loadCustomSourceNames()
{
    QJsonObject config = loadJsonConfig();
    QMap<QString,QString> srcLabels;
    
    if (config.contains("AudioSettings")) {
        QJsonObject audioSettings = config["AudioSettings"].toObject();
        for (auto it = audioSettings.constBegin(); it != audioSettings.constEnd(); ++it) {
            if (it.key().startsWith("CustomSource_")) {
                QString sourceKey = it.key().mid(13); // Remove "CustomSource_" prefix
                QJsonObject labelObj = it.value().toObject();
                if (labelObj.contains("value")) {
                    srcLabels[sourceKey] = labelObj["value"].toString();
                }
            }
        }
    }
    
    // Debug logging reduced to prevent spam
    // m_lib->m_Log->writeLog(4, QString("loadCustomSourceNames: loaded %1 custom source labels").arg(srcLabels.size()));
    m_lib->m_AudioRouter->setCustomSourceLables(srcLabels);
}

void Settings::saveCustomDestinationNames()
{
    QJsonObject config = loadJsonConfig();
    QJsonObject audioSettings;
    if (config.contains("AudioSettings")) {
        audioSettings = config["AudioSettings"].toObject();
    }
    
    QMap<QString,QString> dstLabels = m_lib->m_AudioRouter->getCustomDestLables();
    for (auto i = dstLabels.constBegin(); i != dstLabels.constEnd(); ++i) {
        QJsonObject labelObj;
        labelObj["type"] = 1;  // String type wie in test1.json
        labelObj["value"] = i.value();
        audioSettings[QString("CustomDest_%1").arg(i.key())] = labelObj;
    }
    
    config["AudioSettings"] = audioSettings;
    saveJsonConfig(config);
    
    // m_lib->m_Log->writeLog(3, QString("saveCustomDestinationNames: saved %1 custom destination labels").arg(dstLabels.size()));
}

void Settings::loadCustomDestinationNames()
{
    QJsonObject config = loadJsonConfig();
    QMap<QString,QString> dstLabels;
    
    if (config.contains("AudioSettings")) {
        QJsonObject audioSettings = config["AudioSettings"].toObject();
        for (auto it = audioSettings.constBegin(); it != audioSettings.constEnd(); ++it) {
            if (it.key().startsWith("CustomDest_")) {
                QString destKey = it.key().mid(11); // Remove "CustomDest_" prefix
                QJsonObject labelObj = it.value().toObject();
                if (labelObj.contains("value")) {
                    dstLabels[destKey] = labelObj["value"].toString();
                }
            }
        }
    }
    
    // Debug logging reduced to prevent spam
    // m_lib->m_Log->writeLog(4, QString("loadCustomDestinationNames: loaded %1 custom destination labels").arg(dstLabels.size()));
    m_lib->m_AudioRouter->setCustomDestinationLables(dstLabels);
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
    item["min"] = 1;
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
    m_lib->epCfg.medConfig.sndUseSwClock = true;
    m_lib->epCfg.medConfig.quality =8;
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

     for (const auto &codec : m_lib->m_pjEp->codecEnum2())
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
