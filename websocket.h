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

/*
 * Struktur JSON für Websockets API
 *
 * -befehl
 *  -id / name
 *
 * -data:
 *  -equivalente daten des Befehls
 *
 * Bsp:
 * {
 *      "command": "createAccount",
 *      ,
 *      "data": {
 *          "accountName":"AdisSupernummere",
 *          server:
 *      }
 * }
 *
 *
 * */

#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <QObject>
#include <QMetaObject>
#include "types.h"

QT_FORWARD_DECLARE_CLASS(QWebSocketServer)
QT_FORWARD_DECLARE_CLASS(QWebSocket)
QT_FORWARD_DECLARE_CLASS(AWAHSipLib)

class Websocket : public QObject
{
    Q_OBJECT  
public:
    explicit Websocket(quint16 port, AWAHSipLib *parentLib, QObject *parent = nullptr);
    ~Websocket() override;

private slots:
    void onNewConnection();
    void processMessage(const QString &message);
    void socketDisconnected();

    /**
     * Implementation-Functions for API-Command Executions
     * @param &data JSON-Object with the supplied Data from Websocket
     * @param &ret JSON-Object which contains Data and Error Object
     */
    void echo(QJsonObject &data, QJsonObject &ret);

    /**
     * use this function to initialize a new client with the current stae
     * all relevant variables will be transmitted (e.g. Accounts, Sounddevices etc.)
     * @param &data JSON-Object with the supplied Data from Websocket
     * @param &ret JSON-Object which contains Data and Error Object
     */
    void getAllVariables(QJsonObject &data, QJsonObject &ret);
    // Public API - Accounts
    void createAccount(QJsonObject &data, QJsonObject &ret);
    void modifyAccount(QJsonObject &data, QJsonObject &ret);
    void removeAccount(QJsonObject &data, QJsonObject &ret);
    void getAccounts(QJsonObject &data, QJsonObject &ret);
    void makeCall(QJsonObject &data, QJsonObject &ret);
    void hangupCall(QJsonObject &data, QJsonObject &ret);
    void acceptCall(QJsonObject &data, QJsonObject &ret);
    void holdCall(QJsonObject &data, QJsonObject &ret);
    void transferCall(QJsonObject &data, QJsonObject &ret);
    void getCallInfo(QJsonObject &data, QJsonObject &ret);
    void getCallHistory(QJsonObject &data, QJsonObject &ret);
    void getAccountByID(QJsonObject &data, QJsonObject &ret);
    // Public API - AudioRouter
    void getAudioRoutes(QJsonObject &data, QJsonObject &ret);
    void listInputSoundDev(QJsonObject &data, QJsonObject &ret);
    void listOutputSoundDev(QJsonObject &data, QJsonObject &ret);
    void addAudioDevice(QJsonObject &data, QJsonObject &ret);
    void removeAudioDevice(QJsonObject &data, QJsonObject &ret);
    void addFilePlayer(QJsonObject &data, QJsonObject &ret);
    void addFileRecorder(QJsonObject &data, QJsonObject &ret);
    void getConfPortsList(QJsonObject &data, QJsonObject &ret);
    void connectConfPort(QJsonObject &data, QJsonObject &ret);
    void disconnectConfPort(QJsonObject &data, QJsonObject &ret);
    void changeConfPortLevel(QJsonObject &data, QJsonObject &ret);
    void addToneGen(QJsonObject &data, QJsonObject &ret);
    void getAudioDevices(QJsonObject &data, QJsonObject &ret);
    void getSoundDevID(QJsonObject &data, QJsonObject &ret);

    // Public API - Buddies
    void registerBuddy(QJsonObject &data, QJsonObject &ret);
    void deleteBuddy(QJsonObject &data, QJsonObject &ret);
    // Public API - Codecs
    void listCodec(QJsonObject &data, QJsonObject &ret);
    void selectCodec(QJsonObject &data, QJsonObject &ret);
    void getCodecParam(QJsonObject &data, QJsonObject &ret);
    void setCodecParam(QJsonObject &data, QJsonObject &ret);
    // Public API - Log
    void readNewestLog(QJsonObject &data, QJsonObject &ret);
    // Public API - MessageManager
    void sendDtmf(QJsonObject &data, QJsonObject &ret);
    // Public API - Settings
    void getSettings(QJsonObject &data, QJsonObject &ret);
    void setSettings(QJsonObject &data, QJsonObject &ret);
    void getCodecPriorities(QJsonObject &data, QJsonObject &ret);
    void setSCodecPriorities(QJsonObject &data, QJsonObject &ret);

    /**
     * Implementation-Functions for API-Signals
     *
     */
public slots:
    void regStateChanged(int accId, bool status);
    void sipStatus(int accId, int status, QString remoteUri);
    void callStateChanged(int accID, int role, int callId, bool remoteofferer, long calldur, int state, int lastStatusCode, QString statustxt, QString remoteUri);
    void buddyStatus(QString buddy, int status);
    //void message(QString type, QByteArray message);
    void logMessage(QString msg);
    void audioRoutesChanged(const QList<s_audioRoutes>& audioRoutes);
    void audioRoutesTableChanged(const s_audioPortList& portList);
    void callInfo(int accId, int callId, QJsonObject callInfo);
    void AccountsChanged(QList <s_account>* Accounts);
    void AudioDevicesChanged(QList<s_IODevices>* audioDev);

private:
    AWAHSipLib* m_lib;
    QWebSocketServer *m_pWebSocketServer;
    QList<QWebSocket *> m_clients;
    bool objectFromString(const QString& in, QJsonObject &obj);
    void sendToAll(QJsonObject &obj);

};

typedef  bool (Websocket::*pCmdImplementationFn_t)(QWebSocket*, QJsonObject&);

#endif // WEBSOCKET_H
