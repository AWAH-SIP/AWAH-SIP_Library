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

#include "websocket.h"
#include "awahsiplib.h"

#include <QtWebSockets>
#include <QtCore>


static QString getIdentifier(QWebSocket *peer)
{
    return QStringLiteral("%1:%2").arg(peer->peerAddress().toString(), QString::number(peer->peerPort()));
}

static QJsonObject noError() {
    QJsonObject error;
    error["code"] = false;
    error["string"] = "No Error";
    return error;
}

static QJsonObject hasError(QString errorString) {
    QJsonObject error;
    error["code"] = true;
    error["string"] = errorString;
    return error;
}

static bool jCheckBool(bool &ret, QJsonValueRef val) {
    if(val.isBool()){
        ret = val.toBool();
        return true;
    } else return false;
}

static bool jCheckInt(int &ret, QJsonValueRef val) {
    if(val.isDouble()){
        ret = val.toInt();
        return true;
    } else return false;
}

static bool jCheckString(QString &ret, QJsonValueRef val) {
    if(val.isString()){
        ret = val.toString();
        return true;
    } else return false;
}

static bool jCheckArray(QJsonArray &ret, QJsonValueRef val) {
    if(val.isArray()){
        ret = val.toArray();
        return true;
    } else return false;
}

static bool jCheckObject(QJsonObject &ret, QJsonValueRef val) {
    if(val.isObject()){
        ret = val.toObject();
        return true;
    } else return false;
}

Websocket::Websocket(quint16 port, AWAHSipLib *parentLib, QObject *parent) : QObject(parent),  m_lib(parentLib),
    m_pWebSocketServer(new QWebSocketServer(QStringLiteral("Chat Server"), QWebSocketServer::NonSecureMode, this))
{
    if (m_pWebSocketServer->listen(QHostAddress::Any, port))
    {
        m_lib->m_Log->writeLog(3, QString("Websocket-Server started and listening on port %1").arg(port));
        connect(m_pWebSocketServer, &QWebSocketServer::newConnection,
                this, &Websocket::onNewConnection);
    }
}

Websocket::~Websocket()
{
    m_pWebSocketServer->close();
}

void Websocket::onNewConnection()
{
    auto pSocket = m_pWebSocketServer->nextPendingConnection();
    m_lib->m_Log->writeLog(3, QString("Websocket-Server: %1 connected!").arg(getIdentifier(pSocket)));
    pSocket->setParent(this);

    connect(pSocket, &QWebSocket::textMessageReceived,
            this, &Websocket::processMessage);
    connect(pSocket, &QWebSocket::disconnected,
            this, &Websocket::socketDisconnected);

    m_clients << pSocket;
}

void Websocket::processMessage(const QString &message)
{
    QWebSocket *pSender = qobject_cast<QWebSocket *>(sender());
    m_lib->m_Log->writeLog(4, QString("Websocket  RX:  %1 \n %2").arg(getIdentifier(pSender), message));
    QJsonObject jObj, ret;
    if(objectFromString(message, jObj)) {
        QString command, cmdID;
        QJsonObject data;
        if(jCheckString(command, jObj["command"]) && jCheckObject(data, jObj["data"])) {
            if(QMetaObject::invokeMethod(this, command.toStdString().c_str(), Qt::DirectConnection, Q_ARG(QJsonObject &, data), Q_ARG(QJsonObject &, ret))) {
                if(jCheckString(cmdID, jObj["cmdID"]))
                    ret["cmdID"] = cmdID;
                ret["command"] = jObj["command"].toString();
                pSender->sendTextMessage(QJsonDocument(ret).toJson(QJsonDocument::Compact));
            } else {
                ret["command"] = jObj["command"].toString();
                ret["error"] = hasError("Command '" + command + "' could not be invoked!");
                pSender->sendTextMessage(QJsonDocument(ret).toJson(QJsonDocument::Compact));
            }
        } else {
            ret["error"] = hasError("JSON does not have a 'command' and a 'data' Object");
            pSender->sendTextMessage(QJsonDocument(ret).toJson(QJsonDocument::Compact));
        }
    } else {
        ret["error"] = hasError("Not valid JSON");
        pSender->sendTextMessage(QJsonDocument(ret).toJson(QJsonDocument::Compact));
    }
    m_lib->m_Log->writeLog(4, QString("Websocket  TX:  %1 \n %2").arg(getIdentifier(pSender), QJsonDocument(ret).toJson(QJsonDocument::Compact).toStdString().c_str()));
}

void Websocket::socketDisconnected()
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    m_lib->m_Log->writeLog(3, QString("Websocket-Server: %1 disconnected!").arg(getIdentifier(pClient)));
    if (pClient)
    {
        m_clients.removeAll(pClient);
        pClient->deleteLater();
    }
}

// Implementation-Functions for API-Command Executions
void Websocket::echo(QJsonObject &data, QJsonObject &ret) {
    ret["data"] = data;
    ret["error"] = noError();
}

void Websocket::getAllVariables(QJsonObject &data, QJsonObject &ret){
    Q_UNUSED(data);
    QJsonObject retDataObj;
    QJsonArray accountsArr, audioDevArr;
    pj::AccountInfo accInfo;
    QList <s_account>* accounts = m_lib->getAccounts();
    for (auto & account : *accounts) {
        accountsArr.append(account.toJSON());
    }
    retDataObj["accountsArray"] = accountsArr;
    QList<s_IODevices>* audioDevs = m_lib->getAudioDevices();
    for (auto & audioDev : *audioDevs) {
        audioDevArr.append(audioDev.toJSON());
    }
    retDataObj["audioDevicesArray"] = audioDevArr;
    const s_audioPortList& confPortList = m_lib->getConfPortsList();
    retDataObj["confPortList"] = confPortList.toJSON();

    QJsonArray audioRoutesArr;
    QList <s_audioRoutes> audioRoutes = m_lib->getAudioRoutes();
    for (auto & route : audioRoutes) {
        audioRoutesArr.append(route.toJSON());
    }
    retDataObj["audioRoutesArray"] = audioRoutesArr;
    ret["data"] = retDataObj;
    ret["error"] = noError();
}

void Websocket::createAccount(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    QString accountName, server, user, password, filePlayPath, fileRecPath;
    bool fixedJitterBuffer = true;
    int fixedJitterBufferValue = 0;
    if(     jCheckString(accountName, data["accountName"]) &&
            jCheckString(server, data["server"]) &&
            jCheckString(user, data["user"]) &&
            jCheckString(password, data["password"]) &&
            jCheckString(filePlayPath, data["filePlayPath"]) &&
            jCheckString(fileRecPath, data["fileRecPath"]) &&
            jCheckBool(fixedJitterBuffer, data["fixedJitterBuffer"]) &&
            jCheckInt(fixedJitterBufferValue, data["fixedJitterBufferValue"]) ) {
        m_lib->createAccount(accountName, server, user, password, filePlayPath, fileRecPath, fixedJitterBuffer, fixedJitterBufferValue);
        ret["data"] = retDataObj;
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::modifyAccount(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int index;
    QString accountName, server, user, password, filePlayPath, fileRecPath;
    bool fixedJitterBuffer = true;
    int fixedJitterBufferValue = 0;
    if(     jCheckInt(index, data["index"]) &&
            jCheckString(accountName, data["accountName"]) &&
            jCheckString(server, data["server"]) &&
            jCheckString(user, data["user"]) &&
            jCheckString(password, data["password"]) &&
            jCheckString(filePlayPath, data["filePlayPath"]) &&
            jCheckString(fileRecPath, data["fileRecPath"]) &&
            jCheckBool(fixedJitterBuffer, data["fixedJitterBuffer"]) &&
            jCheckInt(fixedJitterBufferValue, data["fixedJitterBufferValue"]) ) {
        m_lib->modifyAccount(index, accountName, server, user, password, filePlayPath, fileRecPath, fixedJitterBuffer, fixedJitterBufferValue);
        ret["data"] = retDataObj;
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::removeAccount(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int index;
    if (jCheckInt(index, data["index"])) {
        m_lib->removeAccount(index);
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::getAccounts(QJsonObject &data, QJsonObject &ret) {          // todo -> remove me?
    Q_UNUSED(data);
    QJsonObject retDataObj;
    QJsonArray accountsArr;
    QList <s_account>* accounts = m_lib->getAccounts();
    for (auto & account : *accounts) {
        accountsArr.append(account.toJSON());
    }
    retDataObj["accountsArray"] = accountsArr;
    ret["data"] = retDataObj;
    ret["error"] = noError();
}

void Websocket::makeCall(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    QString number;
    int AccID;
    if (jCheckString(number, data["number"]) && jCheckInt(AccID, data["AccID"])) {
        m_lib->makeCall(number, AccID);
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::hangupCall(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int callId, AccID;
    if (jCheckInt(callId, data["callId"]) && jCheckInt(AccID, data["AccID"])) {
        m_lib->hangupCall(callId, AccID);
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::acceptCall(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int callId, AccID;
    if (jCheckInt(callId, data["callId"]) && jCheckInt(AccID, data["AccID"])) {
        m_lib->acceptCall(callId, AccID);
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::holdCall(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int callId, AccID;
    if (jCheckInt(callId, data["callId"]) && jCheckInt(AccID, data["AccID"])) {
        m_lib->holdCall(callId, AccID);
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::transferCall(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int callId, AccID;
    QString destination;
    if (jCheckInt(callId, data["callId"]) && jCheckInt(AccID, data["AccID"]) && jCheckString(destination, data["destination"])) {
        m_lib->transferCall(callId, AccID, destination);
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::getCallInfo(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int callId, AccID;
    if (jCheckInt(callId, data["callId"]) && jCheckInt(AccID, data["AccID"])) {
        QJsonObject retVal = m_lib->getCallInfo(callId, AccID);
        retDataObj["callHistoryObj"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::getCallHistory(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int AccID;
    if (jCheckInt(AccID, data["AccID"])) {
        QJsonArray callHistoryArr;
        const QList<s_callHistory>* callHistoryList = m_lib->getCallHistory(AccID);
        for (auto & callHistEntry : *callHistoryList) {
            callHistoryArr.append(callHistEntry.toJSON());
        }
        retDataObj["callHistoryList"] = callHistoryArr;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::getAccountByID(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int ID;
    if (jCheckInt(ID, data["ID"])) {
        const s_account* pAccount = m_lib->getAccountByID(ID);
        retDataObj["Account"] = pAccount->toJSON();
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::getAudioRoutes(QJsonObject &data, QJsonObject &ret) {
    Q_UNUSED(data);
    QJsonObject retDataObj;
    QJsonArray audioRoutesArr;
    QList <s_audioRoutes> audioRoutes = m_lib->getAudioRoutes();
    for (auto & route : audioRoutes) {
        audioRoutesArr.append(route.toJSON());
    }
    retDataObj["audioRoutesArray"] = audioRoutesArr;
    ret["data"] = retDataObj;
    ret["error"] = noError();
}

void Websocket::listInputSoundDev(QJsonObject &data, QJsonObject &ret) {
    Q_UNUSED(data);
    QJsonObject retDataObj;
    QJsonArray soundDevArr;
    QStringList soundInputDevList = m_lib->listInputSoundDev();
    for (auto & soundDev : soundInputDevList) {
        soundDevArr.append(soundDev);
    }
    retDataObj["soundInputDeviceArray"] = soundDevArr;
    ret["data"] = retDataObj;
    ret["error"] = noError();
}

void Websocket::listOutputSoundDev(QJsonObject &data, QJsonObject &ret) {
    Q_UNUSED(data);
    QJsonObject retDataObj;
    QJsonArray soundDevArr;
    QStringList soundOutputDevList = m_lib->listOutputSoundDev();
    for (auto & soundDev : soundOutputDevList) {
        soundDevArr.append(soundDev);
    }
    retDataObj["soundOutputDeviceArray"] = soundDevArr;
    ret["data"] = retDataObj;
    ret["error"] = noError();
}

void Websocket::addAudioDevice(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int recordDevId, playbackDevId;
    if (jCheckInt(recordDevId, data["recordDevId"]) && jCheckInt(playbackDevId, data["playbackDevId"])) {
        int retVal = m_lib->addAudioDevice(recordDevId, playbackDevId);
        retDataObj["returnValue"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::removeAudioDevice(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int DevIndex;
    if (jCheckInt(DevIndex, data["DevIndex"])) {
        int retVal = m_lib->removeAudioDevice(DevIndex);
        retDataObj["returnValue"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::addFilePlayer(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    QString Name, File;
    if (jCheckString(Name, data["Name"]) && jCheckString(File, data["File"])) {
        int retVal = m_lib->addFilePlayer(Name, File);
        retDataObj["returnValue"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::addFileRecorder(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    QString File;
    if (jCheckString(File, data["File"])) {
        int retVal = m_lib->addFileRecorder(File);
        retDataObj["returnValue"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::getConfPortsList(QJsonObject &data, QJsonObject &ret) {
    Q_UNUSED(data);
    QJsonObject retDataObj;
    const s_audioPortList& confPortList = m_lib->getConfPortsList();
    retDataObj["confPortList"] = confPortList.toJSON();
    ret["data"] = retDataObj;
    ret["error"] = noError();
}

void Websocket::connectConfPort(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int src_slot, sink_slot;
    QString levelStr;
    float level;
    bool persistant;
    if (    jCheckInt(src_slot, data["src_slot"]) &&
            jCheckInt(sink_slot, data["sink_slot"]) &&
            jCheckString(levelStr, data["level"]) &&
            jCheckBool(persistant, data["persistant"]) ) {
        level = levelStr.toFloat();
        int retVal = m_lib->connectConfPort(src_slot, sink_slot, level, persistant);
        retDataObj["returnValue"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::disconnectConfPort(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int src_slot, sink_slot;
    if (jCheckInt(src_slot, data["src_slot"]) && jCheckInt(sink_slot, data["sink_slot"])) {
        int retVal = m_lib->disconnectConfPort(src_slot, sink_slot);
        retDataObj["returnValue"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::changeConfPortLevel(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int src_slot, sink_slot;
    QString levelStr;
    float level;
    if (    jCheckInt(src_slot, data["src_slot"]) &&
            jCheckInt(sink_slot, data["sink_slot"]) &&
            jCheckString(levelStr, data["level"])) {
        level = levelStr.toFloat();
        int retVal = m_lib->changeConfPortLevel(src_slot, sink_slot, level);
        retDataObj["returnValue"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::addToneGen(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int freq;
    if (jCheckInt(freq, data["freq"])) {
        int retVal = m_lib->addToneGen(freq);
        retDataObj["returnValue"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::getAudioDevices(QJsonObject &data, QJsonObject &ret) {
    Q_UNUSED(data);
    QJsonObject retDataObj;
    QJsonArray audioDevArr;
    QList<s_IODevices>* audioDevs = m_lib->getAudioDevices();
    for (auto & audioDev : *audioDevs) {
        audioDevArr.append(audioDev.toJSON());
    }
    retDataObj["audioDevicesArray"] = audioDevArr;
    ret["data"] = retDataObj;
    ret["error"] = noError();
}

void Websocket::getSoundDevID(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    QString DeviceName;
    if (jCheckString(DeviceName, data["DeviceName"])){
        int retVal = m_lib->getSoundDevID(DeviceName);
        retDataObj["returnValue"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::registerBuddy(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int AccID;
    QString buddyUrl;
    if (jCheckInt(AccID, data["AccID"]) && jCheckString(buddyUrl, data["buddyUrl"])) {
        bool retVal = m_lib->registerBuddy(AccID, buddyUrl);
        retDataObj["returnValue"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::deleteBuddy(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int AccID;
    QString buddyUrl;
    if (jCheckInt(AccID, data["AccID"]) && jCheckString(buddyUrl, data["buddyUrl"])) {
        bool retVal = m_lib->deleteBuddy(AccID, buddyUrl);
        retDataObj["returnValue"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::listCodec(QJsonObject &data, QJsonObject &ret) {
    Q_UNUSED(data);
    QJsonObject retDataObj;
    QJsonArray codecArr;
    QStringList codecList = m_lib->listCodec();
    for (auto & codecEntry : codecList) {
        codecArr.append(codecEntry);
    }
    retDataObj["codecArray"] = codecArr;
    ret["data"] = retDataObj;
    ret["error"] = noError();
}

void Websocket::selectCodec(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    QString selectedcodec;
    if (jCheckString(selectedcodec, data["selectedcodec"])) {
        m_lib->selectCodec(selectedcodec);
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::getCodecParam(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    QString codecId;
    if (jCheckString(codecId, data["codecId"])) {
        QJsonObject retVal = m_lib->getCodecParam(codecId);
        retDataObj["codecParamObj"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::setCodecParam(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    QString codecId;
    QJsonObject codecParam;
    if (jCheckString(codecId, data["codecId"]) && jCheckObject(codecParam, data["codecParam"])) {
        int retVal = m_lib->setCodecParam(codecId, codecParam);
        retDataObj["returnValue"] = retVal;
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::readNewestLog(QJsonObject &data, QJsonObject &ret) {
    Q_UNUSED(data);
    QJsonObject retDataObj;
    QJsonArray logArr;
    QStringList logList = m_lib->readNewestLog();
    for (auto & logEntry : logList) {
        logArr.append(logEntry);
    }
    retDataObj["logArray"] = logArr;
    ret["data"] = retDataObj;
    ret["error"] = noError();
}

void Websocket::sendDtmf(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    int callId, AccID;
    QString num;
    if (jCheckInt(callId, data["callId"]) && jCheckInt(AccID, data["AccID"]) && jCheckString(num, data["num"])) {
        m_lib->sendDtmf(callId, AccID, num);
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::getSettings(QJsonObject &data, QJsonObject &ret) {
    Q_UNUSED(data);
    QJsonObject retDataObj;
    const QJsonObject *retVal = m_lib->getSettings();
    retDataObj["settingsObj"] = *retVal;
    ret["data"] = retDataObj;
    ret["error"] = noError();
}

void Websocket::setSettings(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    QJsonObject editedSetting;
    if (jCheckObject(editedSetting, data["editedSetting"])) {
        m_lib->setSettings(editedSetting);
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}

void Websocket::getCodecPriorities(QJsonObject &data, QJsonObject &ret) {
    Q_UNUSED(data);
    QJsonObject retDataObj;
    const QJsonObject retVal = m_lib->getCodecPriorities();
    retDataObj["codecPrioritiesObj"] = retVal;
    ret["data"] = retDataObj;
    ret["error"] = noError();
}

void Websocket::setSCodecPriorities(QJsonObject &data, QJsonObject &ret) {
    QJsonObject retDataObj;
    QJsonObject Codecpriority;
    if (jCheckObject(Codecpriority, data["Codecpriority"])) {
        m_lib->setSettings(Codecpriority);
        ret["data"] = retDataObj;
        ret["error"] = noError();
    } else {
        ret["error"] = hasError("Parameters not accepted");
    }
}


// Implementation-Functions for API-Signals
void Websocket::regStateChanged(int accId, bool status){
    QJsonObject obj, data;
    data["accId"] = accId;
    data["status"] = status;
    obj["signal"] = "regStateChanged";
    obj["data"] = data;
    sendToAll(obj);
}

void Websocket::sipStatus(int accId, int status, QString remoteUri){
    QJsonObject obj, data;
    data["accId"] = accId;
    data["status"] = status;
    data["remoteUri"] = remoteUri;
    obj["signal"] = "sipStatus";
    obj["data"] = data;
    sendToAll(obj);
}

void Websocket::callStateChanged(int accID, int role, int callId, bool remoteofferer, long calldur, int state, int lastStatusCode, QString statustxt, QString remoteUri){
    QJsonObject obj, data;
    data["accId"] = accID;
    data["role"] = role;
    data["callId"] = callId;
    data["remoteofferer"] = remoteofferer;
    data["calldur"] = QString::number(calldur);
    data["state"] = state;
    data["lastStatusCode"] = lastStatusCode;
    data["statustxt"] = statustxt;
    data["remoteUri"] = remoteUri;
    obj["signal"] = "callStateChanged";
    obj["data"] = data;
    sendToAll(obj);
}

void Websocket::callInfo(int accId, int callId, QJsonObject callInfo){
    QJsonObject obj, data;
    data["accId"] = accId;
    data["callId"] = callId;
    data["callInfo"] = callInfo;
    obj["signal"] = "callInfo";
    obj["data"] = data;
    sendToAll(obj);
}

void Websocket::buddyStatus(QString buddy, int status){
    QJsonObject obj, data;
    data["buddy"] = buddy;
    data["status"] = status;
    obj["signal"] = "buddyStatus";
    obj["data"] = data;
    sendToAll(obj);
}

void Websocket::logMessage(QString msg){
    QJsonObject obj, data;
    data["msg"] = msg;
    obj["signal"] = "logMessage";
    obj["data"] = data;
    sendToAll(obj);
}

void Websocket::audioRoutesChanged(const QList<s_audioRoutes>& audioRoutes){
    QJsonObject obj, data;
    QJsonArray audioRoutesArr;
    for (auto & audioRoute: audioRoutes) {
        audioRoutesArr.append(audioRoute.toJSON());
    }
    data["audioRoutes"] = audioRoutesArr;
    obj["signal"] = "audioRoutesChanged";
    obj["data"] = data;
    sendToAll(obj);
}

void Websocket::audioRoutesTableChanged(const s_audioPortList& portList){
    QJsonObject obj, data;
    data["portList"] = portList.toJSON();
    obj["signal"] = "audioRoutesTableChanged";
    obj["data"] = data;
    sendToAll(obj);
}

void Websocket::AccountsChanged(QList <s_account>* Accounts){
    QJsonObject obj, data;
    QJsonArray accountsArr;
    for (auto & account : *Accounts) {
        accountsArr.append(account.toJSON());
    }
    data["Accounts"] = accountsArr;
    obj["signal"] = "AccountsChanged";
    obj["data"] = data;
    sendToAll(obj);
}

void Websocket::AudioDevicesChanged(QList<s_IODevices>* audioDev){
    QJsonObject obj, data;
    QJsonArray audioDevArr;
    for (auto & device : *audioDev) {
        audioDevArr.append(device.toJSON());
    }
    data["AudioDevices"] = audioDevArr;
    obj["signal"] = "AudioDevicesChanged";
    obj["data"] = data;
    sendToAll(obj);
}

bool Websocket::objectFromString(const QString& in, QJsonObject &obj)
{
    QJsonDocument doc = QJsonDocument::fromJson(in.toUtf8());
    // check validity of the document
    if(!doc.isNull()) {
        if(doc.isObject()) {
            obj = doc.object();
        } else {
            qDebug() << "Document is not an object" << Qt::endl;
            return false;
        }
    } else {
        qDebug() << "Invalid JSON...\n" << in << Qt::endl;
        return false;
    }
    return true;
}

void Websocket::sendToAll(QJsonObject &obj) {
    obj["error"] = noError();
    for (QWebSocket *pClient : qAsConst(m_clients)) {
        pClient->sendTextMessage(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }
}
