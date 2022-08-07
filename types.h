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

#ifndef TYPES_H
#define TYPES_H

#include <QUuid>
#include <QObject>
#include <QMap>
#include <QVariant>
#include <QtCore/QDataStream>
#include <QJsonObject>
#include <QJsonArray>

#include <pjsua2.hpp>

extern "C" {
#include <pjlib.h>
#include <pjlib-util.h>
#include <pjmedia.h>
#include <pjmedia-codec.h>
#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjsip_ua.h>
#include <pjsua-lib/pjsua.h>
#include <pj/string.h>
}

#include "pjaccount.h"
#include "pjbuddy.h"
#include "pjcall.h"
#include "pjlogwriter.h"

class GpioDevice;
class AccountGpioDev;

inline QString createNewUID() { return QUuid::createUuid().toString(QUuid::Id128); }

inline pj_str_t str2Pj(const QString &input_str)
{
    pj_str_t output_str;
    output_str.ptr = (char*)input_str.toStdString().c_str();
    output_str.slen = input_str.size();
    return output_str;
}

inline QString pj2Str(const pj_str_t &input_str)
{
    if (input_str.ptr && input_str.slen>0)
        return QString::fromStdString(std::string(input_str.ptr, input_str.slen));
    return QString();
}



inline int dBtoAdjLevel(int &level)
{
    int leveladjust = 0;
    // conversion table from dB to pjusa leveladjust, this is used because I din't find any algorithm to match the dB values over the whole range
    QHash<int,int> const m_dbToAdjustVol = {{20,1152},{19,1013},{18,889},{17,778},{16,680},{15,592},{14,514},{13,444},{12,382},{11,326},
                                            {10,277},{9,233},{8,193},{7,157},{6,128},{5,99},{4,75},{3,51},{2,33},{1,15},{0,0},
                                            {-1,-14},{-2,-26},{-3,-37},{-4,-47},{-5,-56},{-6,-64},{-7,-71},{-8,-77},{-9,-83},{-10,-88},
                                            {-11,-92},{-12,-96},{-13,-99},{-14,-103},{-15,-105},{-16,-108},{-17,-110},{-18,-112},{-19,-114},{-20,-115},
                                            {-21,-117},{-22,-118},{-23,-119},{-24,-120},{-25,-121},{-26,-122},{-28,-123},
                                            {-30,-124},{-32,-125},{-36,-126},
                                            {-42,-127}};            // levels below -42 are not supported, mute = -128

    if(level > 20) level = 20;              // limit the maximum gain to 20dB
    if(level < -42){                        // values below -40 are not supported by pjsua, so mute the xp
        leveladjust = -128;
        level = -96;
    }
    switch (level) {                        // some levels can not be set due to poor resolution so the level is mapped to the next closest
    case -27:
        level = -28;
        break;
    case -29:
        level = -30;
        break;
    case -31:
        level = -32;
        break;
    case -33:
    case -34:
    case -35:
        level = -36;
        break;
    case -37:
    case -38:
    case -39:
    case -40:
    case -41:
        level = -42;
        break;
    }
    if(m_dbToAdjustVol.contains(level)){
        leveladjust = m_dbToAdjustVol.value(level);
    }
    return leveladjust;
}

inline float dBtoFact(const float dB)
{
   float factor = pow(10, (dB/20));
   return factor;
}

inline QString sizeFormat(quint64 size)
{
    qreal calc = size;
    QStringList list;
    list << "KB" << "MB" << "GB" << "TB";

    QStringListIterator i(list);
    QString unit("byte(s)");

    while(calc >= 1024.0 && i.hasNext())
    {
        unit = i.next();
        calc /= 1024.0;
    }

    return QString().setNum(calc, 'f', 2) + " " + unit;
}

enum DeviceType {
    SoundDevice,
    TestToneGenerator,
    FilePlayer,
    FileRecorder,
    VirtualGpioDevice,
    LogicAndGpioDevice,
    LogicOrGpioDevice,
    AccountGpioDevice,
    LinuxGpioDevice,
    AudioCrosspointDevice
};
Q_ENUMS(DeviceType)

enum GPIOprotocol {
    WebSocketDevice
};
Q_ENUMS(GPIOprotocol)

enum AWAHPresenceState {
    unknown = 0,
    online = 1,
    busy = 2
};
Q_ENUMS(AWAHPresenceState)

struct s_IODevices{
    DeviceType devicetype;
    QString uid ="";
    QString inputname="";
    QString outputame="";
    QList <int> portNo = QList <int>();
    int genfrequency = -1;               // only for devicetype TestToneGenerator
    pjmedia_port* mediaport = nullptr;
    int RecDevID = -1;                   // only for devicetype AudioDevice, -1 indicates an offline device
    int PBDevID = -1;
    QString path = "n/a";                   // ony for devicetype Fileplayer, FileRecorder
    pjmedia_snd_port *soundport =nullptr;
    uint inChannelCount = 0;            // For AudioDevices: not saved, only for listConfPorts
    uint outChannelCount = 0;           // For AudioDevices: not saved, only for listConfPorts
    QJsonObject typeSpecificSettings = {};
    QJsonObject toJSON() const {
        QJsonArray portNrArr;
        for (auto & port: portNo) {
            portNrArr.append(port);
        }
        return {{"devicetype", devicetype}, {"uid", uid}, {"inputname", inputname}, {"outputname", outputame}, {"portNo", portNrArr},
                {"genfrequency", genfrequency}, {"RecDevID", RecDevID}, {"PBDevID", PBDevID}, {"path", path},
                {"inChannelCount", (int) inChannelCount}, {"outChannelCount", (int) outChannelCount}, {"typeSpecificSettings", typeSpecificSettings}};
    }
    s_IODevices* fromJSON(QJsonObject &ioDeviceJSON) {
        QJsonArray portNrArr = ioDeviceJSON["portNo"].toArray();
        switch (ioDeviceJSON["devicetype"].toInt()) {
        case SoundDevice:
            devicetype = SoundDevice;
            break;
        case TestToneGenerator:
            devicetype = TestToneGenerator;
            break;
        case FilePlayer:
            devicetype = FilePlayer;
            break;
        case FileRecorder:
            devicetype = FileRecorder;
            break;
        case VirtualGpioDevice:
            devicetype = VirtualGpioDevice;
            break;
        case LogicAndGpioDevice:
            devicetype = LogicAndGpioDevice;
            break;
        case LogicOrGpioDevice:
            devicetype = LogicOrGpioDevice;
            break;
        case AccountGpioDevice:
            devicetype = AccountGpioDevice;
            break;
        case LinuxGpioDevice:
            devicetype = LinuxGpioDevice;
            break;
        case AudioCrosspointDevice:
            devicetype = AudioCrosspointDevice;
            break;
        }
        uid = ioDeviceJSON["uid"].toString();
        inputname = ioDeviceJSON["inputname"].toString();
        outputame = ioDeviceJSON["outputname"].toString();
        for (auto portNrObj : portNrArr) {
            portNo.append(portNrObj.toInt());
        }
        genfrequency = ioDeviceJSON["genfrequency"].toInt();
        RecDevID = ioDeviceJSON["RecDevID"].toInt();
        PBDevID = ioDeviceJSON["PBDevID"].toInt();
        path = ioDeviceJSON["path"].toString();
        inChannelCount = (uint) ioDeviceJSON["inChannelCount"].toInt();
        outChannelCount = (uint) ioDeviceJSON["outChannelCount"].toInt();
        typeSpecificSettings = ioDeviceJSON["typeSpecificSettings"].toObject();
        return this;
    }
};
Q_DECLARE_METATYPE(s_IODevices);
Q_DECLARE_METATYPE(QList<s_IODevices>);

struct s_codec{
    QString encodingName = "";
    QString displayName = "";
    int priority = 0;
    QJsonObject codecParameters = QJsonObject();
    QJsonObject toJSON() const {
        return{{"encodingName", encodingName}, {"displayName", displayName}, {"codecParameters", codecParameters}, {"priority", priority}};
    }
    s_codec* fromJSON(const QJsonObject &codecJSON) {
        encodingName = codecJSON["encodingName"].toString();
        displayName = codecJSON["displayName"].toString();
        codecParameters = codecJSON["codecParameters"].toObject();
        priority = codecJSON["priority"].toInt();
        return this;
    }
};
Q_DECLARE_METATYPE(s_codec);
Q_DECLARE_METATYPE(QList<s_codec>);

struct s_callHistory{
    QString callUri = "";
    int duration = 0;
    s_codec codec = s_codec();
    bool outgoing = 0;
    int count = 0;
    QJsonObject toJSON() const {
        return {{"callUri", callUri}, {"duration", duration}, {"codec", codec.toJSON()}, {"outgoing", outgoing}, {"count", count}};
    }
    s_callHistory* fromJSON(const QJsonObject &callHistoryJSON) {
        callUri = callHistoryJSON["callUri"].toString();
        duration = callHistoryJSON["duration"].toInt();
        codec.fromJSON(callHistoryJSON["codec"].toObject());
        outgoing = callHistoryJSON["outgoing"].toBool();
        count = callHistoryJSON["count"].toInt();
        return this;
    }
};
Q_DECLARE_METATYPE(s_callHistory);
Q_DECLARE_METATYPE(QList<s_callHistory>);

struct s_Call{
    explicit s_Call(int &splitterSlot) : splitterSlot(splitterSlot) { };

    int callId = PJSUA_INVALID_ID;
    int lastJBemptyGETevent = 0;
    int RXlostSeconds = 0;      // time in seconds since the last recieved frame
    QString CallStatusText = "Idle... ";
    int CallStatusCode = 0;
    QString ConnectedTo = "";
    pjsua_player_id player_id = PJSUA_INVALID_ID;
    pjsua_recorder_id rec_id = PJSUA_INVALID_ID;
    PJCall* callptr = nullptr;
    s_codec codec = s_codec();
    int splitterSlot;
    QJsonObject toJSON() const {
        return {{"CallStatusText", CallStatusText}, {"CallStatusCode", CallStatusCode}, {"ConnectedTo", ConnectedTo}, {"callId", callId}};
    }
};
//Q_DECLARE_METATYPE(s_Call);
//Q_DECLARE_METATYPE(QList<s_Call>);

struct s_account{
    QString name = "";
    QString user = "";
    QString password = "";
    QString serverURI = "";
    QString uid = "";
    QString FileRecordPath = "";
    bool FileRecordRXonly = true;
    QString FilePlayPath = "";
    QString autoconnectToBuddyUID = "";
    bool autoconnectEnable = true;
    PJAccount *accountPtr = nullptr;          // not saved to file, only for runtime handling
    AccountGpioDev *gpioDev = nullptr;        // not saved to file, only for runtime handling
    int AccID = PJSUA_INVALID_ID;;
    int splitterSlot = PJSUA_INVALID_ID;
    pjmedia_port *splitComb = nullptr;
    QList <s_Call> CallList = QList <s_Call>();
    QList <s_callHistory> CallHistory = QList <s_callHistory>();
    QString callStatusLastReason = "";
    bool fixedJitterBuffer = true;
    uint fixedJitterBufferValue = 160;
    QString SIPStatusText = "trying to register account...";
    int SIPStatusCode = 0;
    AWAHPresenceState presenceState = unknown;
    QJsonObject toJSON() const {
        QJsonArray callListJSON, callHistoryJSON;
        for (auto & call: CallList) {
            callListJSON.append(call.toJSON());
        }
        for (auto & callhistory: CallHistory) {
            callHistoryJSON.append(callhistory.toJSON());
        }
        return {
            {"name", name},
            {"user", user},
            {"password", password},
            {"serverURI", serverURI},
            {"FileRecordPath", FileRecordPath},
            {"FileRecordRXonly", FileRecordRXonly},
            {"FilePlayPath", FilePlayPath},
            {"AccID", AccID},
            {"CallList", callListJSON},
            {"CallHistory", callHistoryJSON},
            {"fixedJitterBuffer", fixedJitterBuffer},
            {"fixedJitterBufferValue", (int)fixedJitterBufferValue},
            {"SIPStatusCode", SIPStatusCode},
            {"SIPStatusText", SIPStatusText},
            {"callStatusLastReason", callStatusLastReason},
            {"autoconnectToBuddyUID", autoconnectToBuddyUID},
            {"autoconnectEnable", autoconnectEnable},
            {"uid", uid}
        };
    }
    s_account* fromJSON(QJsonObject &accountJSON){
        name = accountJSON["name"].toString();
        user = accountJSON["user"].toString();
        password = accountJSON["password"].toString();
        serverURI = accountJSON["serverURI"].toString();
        FileRecordPath = accountJSON["FileRecordPath"].toString();
        FileRecordRXonly = accountJSON["FileRecordRXonly"].toBool();
        FilePlayPath = accountJSON["FilePlayPath"].toString();
        fixedJitterBuffer = accountJSON["fixedJitterBuffer"].toBool();
        fixedJitterBufferValue = accountJSON["fixedJitterBufferValue"].toInt();
        autoconnectToBuddyUID = accountJSON["AutoconnectToBuddyUID"].toString();
        autoconnectEnable = accountJSON["autoconnectEnable"].toBool();
        return this;
    }
};
Q_DECLARE_METATYPE(s_account);
Q_DECLARE_METATYPE(QList<s_account>);

struct s_audioPort{
    QString name ="";
    pj_str_t pjName;
    int slot =INVALID_ID;
    QJsonObject toJSON() const {
        return {{"name",name}, {"pjName", pj2Str(pjName)}, {"slot",slot}};
    }
    s_audioPort* fromJSON(const QJsonObject &audioPortJSON) {
        name = audioPortJSON["name"].toString();
        pjName = str2Pj(audioPortJSON["pjName"].toString());
        slot = audioPortJSON["slot"].toInt();
        return this;
    }
};
Q_DECLARE_METATYPE(s_audioPort);

struct s_audioPortList{
    QList<s_audioPort> srcPorts = QList<s_audioPort>();
    QList<s_audioPort> destPorts = QList<s_audioPort>();
    QJsonObject toJSON() const {
        QJsonArray srcPortsArr, destPortsArr;
        for (auto & srcPort: srcPorts) {
            srcPortsArr.append(srcPort.toJSON());
        }
        for (auto & destPort: destPorts) {
            destPortsArr.append(destPort.toJSON());
        }
        return {{"srcPorts", srcPortsArr}, {"destPorts", destPortsArr}};
    }
    s_audioPortList* fromJSON(QJsonObject &audioPortListJSON) {
        QJsonArray srcPortArr, destPortArr;
        srcPorts.clear();
        destPorts.clear();
        if(audioPortListJSON["srcPorts"].isArray() && audioPortListJSON["destPorts"].isArray()) {
            s_audioPort entry;
            srcPortArr = audioPortListJSON["srcPorts"].toArray();
            destPortArr = audioPortListJSON["destPorts"].toArray();
            for (auto&& srcPort : srcPortArr) {
                srcPorts.append(*entry.fromJSON(srcPort.toObject()));
            }
            for (auto&& destPort : destPortArr) {
                destPorts.append(*entry.fromJSON(destPort.toObject()));
            }
        }
        return this;
    }
};
Q_DECLARE_METATYPE(s_audioPortList);

struct s_audioRoutes{
    int srcSlot = PJSUA_INVALID_ID;
    int destSlot = PJSUA_INVALID_ID;
    QString srcDevName = "";
    QString destDevName = "";
    int level = 0;
    bool persistant = 0;
    QJsonObject toJSON() const {
        return {{"srcSlot", srcSlot}, {"destSlot", destSlot}, {"srcDevName", srcDevName}, {"destDevName", destDevName}, {"level", level}, {"persistant", persistant} };
    }
    s_audioRoutes fromJSON(QJsonObject &audioRoutesJSON) {
        s_audioRoutes audioroutes;
        srcSlot = audioRoutesJSON["srcSlot"].toInt();
        destSlot = audioRoutesJSON["destSlot"].toInt();
        srcDevName = audioRoutesJSON["srcDevName"].toString();
        destDevName = audioRoutesJSON["destDevName"].toString();
        level = audioRoutesJSON["level"].toVariant().toInt();
        persistant = audioRoutesJSON["persistant"].toBool();
        return audioroutes;

    }
    s_audioRoutes* fromJSON(const QJsonObject &audioRouteJSON) {
        srcSlot = audioRouteJSON["srcSlot"].toInt();
        destSlot = audioRouteJSON["destSlot"].toInt();
        srcDevName = audioRouteJSON["srcDevName"].toString();
        destDevName = audioRouteJSON["destDevName"].toString();
        level = audioRouteJSON["level"].toInt();
        persistant = audioRouteJSON["persistant"].toBool();
        return this;
    }
};
Q_DECLARE_METATYPE(s_audioRoutes);
Q_DECLARE_METATYPE(QList<s_audioRoutes>);

enum settingType{
    INTEGER,
    STRING,
    BOOL_T,
    ENUM_INT,
    ENUM_STRING,
    HIDDEN
};
Q_ENUMS(settingType)

struct s_gpioPort{
    QString name = "";
    QString slotId;
    GpioDevice* device = nullptr;     //NOT in JSON only for InternalRef
    uint channel;           //NOT in JSON only for InternalRef
    bool lastState;         //NOT in JSON only for InternalRef
    QJsonObject toJSON() const {
        return {{"name",name}, {"slotId",slotId}};
    }
    s_gpioPort* fromJSON(const QJsonObject &audioPortJSON) {
        name = audioPortJSON["name"].toString();
        slotId = audioPortJSON["slotId"].toString();
        return this;
    }
};
Q_DECLARE_METATYPE(s_gpioPort);

struct s_gpioPortList{
    QList<s_gpioPort> srcPorts = QList<s_gpioPort>();
    QList<s_gpioPort> destPorts = QList<s_gpioPort>();
    QJsonObject toJSON() const {
        QJsonArray srcPortsArr, destPortsArr;
        for (auto & srcPort: srcPorts) {
            srcPortsArr.append(srcPort.toJSON());
        }
        for (auto & destPort: destPorts) {
            destPortsArr.append(destPort.toJSON());
        }
        return {{"srcPorts", srcPortsArr}, {"destPorts", destPortsArr}};
    }
    s_gpioPortList* fromJSON(QJsonObject &gpioPortListJSON) {
        QJsonArray srcPortArr, destPortArr;
        srcPorts.clear();
        destPorts.clear();
        if(gpioPortListJSON["srcPorts"].isArray() && gpioPortListJSON["destPorts"].isArray()) {
            s_gpioPort entry;
            srcPortArr = gpioPortListJSON["srcPorts"].toArray();
            destPortArr = gpioPortListJSON["destPorts"].toArray();
            for (auto&& srcPort : srcPortArr) {
                srcPorts.append(*entry.fromJSON(srcPort.toObject()));
            }
            for (auto&& destPort : destPortArr) {
                destPorts.append(*entry.fromJSON(destPort.toObject()));
            }
        }
        return this;
    }
};
Q_DECLARE_METATYPE(s_gpioPortList);

struct s_gpioRoute{
    QString srcSlotId = "";
    QString destSlotId = "";
    bool inverted = 0;
    bool persistant = 0;
    QJsonObject toJSON() const {
        return {{"srcSlotId", srcSlotId}, {"destSlotId", destSlotId}, {"inverted", inverted}, {"persistant", persistant} };
    }
    s_gpioRoute* fromJSON(const QJsonObject &audioRouteJSON) {
        srcSlotId = audioRouteJSON["srcSlotId"].toString();
        destSlotId = audioRouteJSON["destSlotId"].toString();
        inverted = audioRouteJSON["inverted"].toBool();
        persistant = audioRouteJSON["persistant"].toBool();
        return this;
    }
};
Q_DECLARE_METATYPE(s_gpioRoute);
Q_DECLARE_METATYPE(QList<s_gpioRoute>);

struct s_buddy{
    QString buddyUrl = "";
    QString Name = "";
    int status = PJSUA_BUDDY_STATUS_UNKNOWN;
    bool autoconnectInProgress = false;
    QString accUid = "";
    s_codec codec;
    QString uid = "";
    uint8_t maxPresenceRefreshTime = 30;
    QDateTime lastSeen = QDateTime();
    QJsonObject toJSON() const {
        return{{"buddyUrl", buddyUrl}, {"Name", Name}, {"status", status}, {"accUid", accUid}, {"codec", codec.toJSON()}, {"uid", uid}};
    }
    s_buddy* fromJSON(const QJsonObject &buddyJSON) {
        buddyUrl = buddyJSON["buddyUrl"].toString();
        Name = buddyJSON["Name"].toString();
        status = buddyJSON["status"].toInt();
        accUid = buddyJSON["accUid"].toString();
        codec.fromJSON(buddyJSON["codec"].toObject());
        uid = buddyJSON["uid"].toString();
        return this;
    }
};
Q_DECLARE_METATYPE(s_buddy);
Q_DECLARE_METATYPE(QList<s_buddy>);

#endif // TYPES_H
