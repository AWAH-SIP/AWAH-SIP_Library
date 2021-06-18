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

inline int dBtoAdjLevel(const float dB)
{
    float factor = pow(10, (dB/20));
    return (int)((factor-1) * 128);
}

inline float dBtoFact(const float dB)
{
   float factor = pow(10, (dB/20));
   return factor;
}

inline float factTodB(const float fact)
{
   float dB = 20 * log10f(fact);
   return dB;
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
Q_DECLARE_METATYPE( QList<s_IODevices>);

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
    QJsonObject codecSettings = QJsonObject();
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
    QString FilePlayPath = "";
    PJAccount *accountPtr = nullptr;          // not saved to file, only for runtime handling
    AccountGpioDev *gpioDev = nullptr;        // not saved to file, only for runtime handling
    int AccID = PJSUA_INVALID_ID;;
    int splitterSlot = PJSUA_INVALID_ID;
    pjmedia_port *splitComb = nullptr;
    QList <s_Call> CallList = QList <s_Call> ();
    QList <s_callHistory> CallHistory = QList <s_callHistory>();
    QString callStatusLastReason = "";
    bool fixedJitterBuffer = true;
    uint fixedJitterBufferValue = 160;
    QString SIPStatusText = "trying to register account...";
    int SIPStatusCode = 0;
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
            {"FilePlayPath", FilePlayPath},
            {"AccID", AccID},
            {"CallList", callListJSON},
            {"CallHistory", callHistoryJSON},
            {"fixedJitterBuffer", fixedJitterBuffer},
            {"fixedJitterBufferValue", (int)fixedJitterBufferValue},
            {"SIPStatusCode", SIPStatusCode},
            {"SIPStatusText", SIPStatusText},
            {"callStatusLastReason", callStatusLastReason}
        };
    }
    s_account* fromJSON(QJsonObject &accountJSON){
        name = accountJSON["name"].toString();
        user = accountJSON["user"].toString();
        password = accountJSON["password"].toString();
        serverURI = accountJSON["serverURI"].toString();
        FileRecordPath = accountJSON["FileRecordPath"].toString();
        FilePlayPath = accountJSON["FilePlayPath"].toString();
        fixedJitterBuffer = accountJSON["fixedJitterBuffer"].toBool();
        fixedJitterBufferValue = accountJSON["fixedJitterBufferValue"].toInt();
        return this;
    }
};
Q_DECLARE_METATYPE(s_account);
Q_DECLARE_METATYPE( QList<s_account>);

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
            for (auto srcPort : srcPortArr) {
                srcPorts.append(*entry.fromJSON(srcPort.toObject()));
            }
            for (auto destPort : destPortArr) {
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
    float level = 0;
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
        level = audioRoutesJSON["level"].toVariant().toFloat();
        persistant = audioRoutesJSON["persistant"].toBool();
        return audioroutes;

    }
    s_audioRoutes* fromJSON(const QJsonObject &audioRouteJSON) {
        srcSlot = audioRouteJSON["srcSlot"].toInt();
        destSlot = audioRouteJSON["destSlot"].toInt();
        srcDevName = audioRouteJSON["srcDevName"].toString();
        destDevName = audioRouteJSON["destDevName"].toString();
        level = audioRouteJSON["level"].toDouble();
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
            for (auto srcPort : srcPortArr) {
                srcPorts.append(*entry.fromJSON(srcPort.toObject()));
            }
            for (auto destPort : destPortArr) {
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
    QString accUid = "";
    QString autoConnectFromAccountUid = "";
    s_codec codec;
    QJsonObject toJSON() const {
        return{{"buddyUrl", buddyUrl}, {"Name", Name}, {"status", status}, {"accUid", accUid}, {"autoConnectFromAccountUid", autoConnectFromAccountUid}, {"codec", codec.toJSON()} };
    }
    s_buddy* fromJSON(const QJsonObject &buddyJSON) {
        buddyUrl = buddyJSON["buddyUrl"].toString();
        Name = buddyJSON["Name"].toString();
        status = buddyJSON["status"].toInt();
        accUid = buddyJSON["accUid"].toString();
        autoConnectFromAccountUid = buddyJSON["autoConnectFromAccountUid"].toString();
        codec.fromJSON(buddyJSON["codecSettings"].toObject());
        return this;
    }
};
Q_DECLARE_METATYPE(s_buddy);
Q_DECLARE_METATYPE(QList<s_buddy>);

#endif // TYPES_H
