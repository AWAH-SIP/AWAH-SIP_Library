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

#ifndef AWAHSIPLIB_H
#define AWAHSIPLIB_H

#include "types.h"

#include <QString>
#include <QDebug>
#include <QObject>

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

#include "pjendpoint.h"
#include "pjaccount.h"
#include "pjbuddy.h"
#include "pjcall.h"
#include "pjlogwriter.h"

#include "accounts.h"
#include "audiorouter.h"
#include "buddies.h"
#include "codecs.h"
#include "gpiodevicemanager.h"
#include "log.h"
#include "messagemanager.h"
#include "settings.h"
#include "websocket.h"



class AWAHSipLib : public QObject
{
    Q_OBJECT
public:
    explicit AWAHSipLib(QObject *parent = nullptr);
    ~AWAHSipLib();

    static void prepareLib();

    // Public API - Accounts
    void createAccount(QString accountName, QString server, QString user, QString password, QString filePlayPath, QString fileRecPath, bool fixedJitterBuffer, uint fixedJitterBufferValue) const
        { return m_Accounts->createAccount(accountName, server, user, password, filePlayPath, fileRecPath, fixedJitterBuffer, fixedJitterBufferValue); };
    void modifyAccount(int index, QString accountName, QString server, QString user, QString password, QString filePlayPath, QString fileRecPath, bool fixedJitterBuffer, uint fixedJitterBufferValue) const
        { return m_Accounts->modifyAccount(index, accountName, server, user, password, filePlayPath, fileRecPath, fixedJitterBuffer, fixedJitterBufferValue); };
    void removeAccount(int index) const { return m_Accounts->removeAccount(index); };
    QList <s_account>* getAccounts() const { return m_Accounts->getAccounts(); };
    void makeCall(QString number, int AccID) const { return m_Accounts->makeCall(number, AccID); };
    void hangupCall(int callId, int AccID) const { return m_Accounts->hangupCall(callId, AccID); };
    void acceptCall(int callId, int AccID) const { return m_Accounts->acceptCall(callId, AccID); };
    void holdCall(int callId, int AccID) const { return m_Accounts->holdCall(callId, AccID); };
    void transferCall(int callId, int AccID, QString destination) const { return m_Accounts->transferCall(callId, AccID, destination); };
    QJsonObject getCallInfo(int callID, int AccID) const { return m_Accounts->getCallInfo(callID, AccID); };
    const QList<s_callHistory>* getCallHistory(int AccID) const { return m_Accounts->getCallHistory(AccID); };
    const s_account* getAccountByID(int ID) {return m_Accounts->getAccountByID(ID); };

    // Public API - AudioRouter
    QList <s_audioRoutes> getAudioRoutes() const { return m_AudioRouter->getAudioRoutes(); };
    QStringList listInputSoundDev() const { return m_AudioRouter->listInputSoundDev(); };
    QStringList listOutputSoundDev() const { return m_AudioRouter->listOutputSoundDev(); };
    int addAudioDevice(int recordDevId, int playbackDevId) const { return m_AudioRouter->addAudioDevice(recordDevId, playbackDevId); };
    int removeAudioDevice(int DevIndex) const { return m_AudioRouter->removeAudioDevice(DevIndex); };
    int addFilePlayer(QString Name, QString File) const { return m_AudioRouter->addFilePlayer(Name, File); };
    int addFileRecorder(QString File) const { return m_AudioRouter->addFileRecorder(File); };
    const s_audioPortList& getConfPortsList() const { return m_AudioRouter->getConfPortsList(); };
    int connectConfPort(int src_slot, int sink_slot, float level, bool persistant = true) const
        { return m_AudioRouter->connectConfPort(src_slot, sink_slot, level, persistant); };
    int disconnectConfPort(int src_slot, int sink_slot) const { return m_AudioRouter->disconnectConfPort(src_slot, sink_slot); };
    int changeConfPortLevel(int src_slot, int sink_slot, float level) const { return m_AudioRouter->changeConfPortLevel(src_slot, sink_slot, level); };
    int addToneGen(int freq) const { return m_AudioRouter->addToneGen(freq); };
    QList<s_IODevices>* getAudioDevices() const { return m_AudioRouter->getAudioDevices(); };
    int getSoundDevID(QString DeviceName) const { return m_AudioRouter->getSoundDevID(DeviceName); };

    // Public API - Buddies
    bool registerBuddy(int AccID, QString buddyUrl) const { return m_Buddies->registerBuddy(AccID, buddyUrl); };
    bool deleteBuddy(int AccID, QString buddyUrl) const { return  m_Buddies->deleteBuddy(AccID, buddyUrl); };

    // Public API - Codecs
    QStringList listCodec() const { return m_Codecs->listCodec(); };
    void selectCodec(QString selectedcodec) const { return m_Codecs->selectCodec(selectedcodec); };
    const QJsonObject getCodecParam(QString codecId) const { return m_Codecs->getCodecParam(codecId); };
    int setCodecParam(QString codecId, QJsonObject codecParam) const { return m_Codecs->setCodecParam(codecId, codecParam); };

    // Public API - GpioDeviceManager
    const VirtualGpioDev* createGpioDev(uint inCount, uint outCount, QString devName)
        { return m_GpioDeviceManager->create(inCount, outCount, devName); };
    const LogicGpioDev* createGpioDev(DeviceType type, uint outCount, QString devName)
        { return m_GpioDeviceManager->create(type, outCount, devName); };
    void removeGpioDevice(QString uid) { return m_GpioDeviceManager->removeDevice(uid); };
    const QList<s_IODevices>& getGpioDevices() const { return m_GpioDeviceManager->getGpioDevices(); };

    // Public API - GpioRouter
    const QList<s_gpioRoute>& getGpioRoutes() { return GpioRouter::instance()->getGpioRoutes(); };
    const s_gpioPortList& getGpioPortsList() { return GpioRouter::instance()->getGpioPortsList(); };
    void connectGpioPort(QString srcSlotId, QString destSlotId, bool inverted, bool persistant = true)
        { return GpioRouter::instance()->connectGpioPort(srcSlotId, destSlotId, inverted, persistant); };
    void disconnectGpioPort(QString srcSlotId, QString destSlotId) { return GpioRouter::instance()->disconnectGpioPort(srcSlotId, destSlotId); };
    void changeGpioCrosspoint(QString srcSlotId, QString destSlotId, bool inverted)
        { return GpioRouter::instance()->changeGpioCrosspoint(srcSlotId, destSlotId, inverted); };

    // Public API - Log
    QStringList readNewestLog() const { return m_Log->readNewestLog();};

    // Public API - MessageManager
    void sendDtmf(int callId, int AccID, QString num) const { return m_MessageManager->sendDtmf(callId, AccID, num); };

    // Public API - Settings
    const QJsonObject* getSettings() const { return m_Settings->getSettings(); };
    void setSettings(QJsonObject editedSetting) { return m_Settings->setSettings(editedSetting); } ;
    const QJsonObject getCodecPriorities() const { return m_Settings->getCodecPriorities(); };
    void setSCodecPriorities(QJsonObject Codecpriority) { return m_Settings->setCodecPriorities(Codecpriority); } ;

    virtual void on_ip_change_progress(pjsua_ip_change_op op, pj_status_t status, const pjsua_ip_change_op_info *info);

public slots:
    void slotSendMessage(int callId, int AccID, QString type, QByteArray message);


signals:

    /**
    * @brief Signal with the state of the accounts (useful for simple signalig e.g. for an LED)
    * @param accId the Id of the account
    * @param status 1 = account is registered, 0 = acoount unregistered
    */
    void regStateChanged(int accId, bool status);

    /**
    * @brief Signal with the SIP state
    * @param accId the Id of the account
    * @param status the SIP status code
    * @param remoteUri the URI of the remote end
    */
    void signalSipStatus(int accId, int status, QString remoteUri);

    /**
    * @brief Signal with state of the calls
    * @param accId the Id of the account
    * @param role
    * @param callID the Id of the call
    * @param remoteofferer if true the call is incoming
    * @param calldur the duration of the call
    * @param state the state of the call (1 to 6) e.g. disconnected
    * @param lastStatusCode the SIP status code
    * @param statustxt the text label to the SIP status code
    * @param remoteUri the URI of the remote end
    */
    void callStateChanged(int accId, int role, int callId, bool remoteofferer, long calldur, int state, int lastStatusCode, QString statustxt, QString remoteUri);

    /**
    * @brief Signal with the status of the buddies
    * @param buddy the buddy
    * @param status the buddy status code
    */
    void signalBuddyStatus(QString buddy, int status);

    /**
    * @brief Signal Message recieved
    * @param type the message type
    * @param message the message data                                                   // todo depending of the type a qvariant of the message could be useful
    */
    void signalMessage(QString type, QByteArray message);

    /**
    * @brief Signal with the log messages for display puroses. They get written to a file automatically
    * @param msg a log message
    */
    void logMessage(QString msg);

    /**
    * @brief Signal if audio routes from the conference-bridge changed
    * @param audioRoutes all routes actually set as a QList
    */
    void audioRoutesChanged(QList<s_audioRoutes> audioRoutes);

    /**
    * @brief Signal if audio route Table from the conference-bridge changed
    * @param portList all Sources and Sinks as Struct
    */
    void audioRoutesTableChanged(const s_audioPortList& portList);

    /**
    * @brief Signal Accounts Changed (e.g. an account is added or deleated)
    * @param Accounts a QList with all the accounts
    */
    void AccountsChanged(QList <s_account>* Accounts);

    /**
    * @brief Signal if audio device config changed
    * @param QList of the new device config
    */
    void AudioDevicesChanged(QList<s_IODevices>* audioDev);

    /**
    * @brief Signal a Callinfo updated every second for every active call
    * @param accId the account identifier of the Call
    * @param CallId the call identifier
    * @param callInfo a JsonObject with various info parameters
    */
    void callInfo(int accId, int callId, QJsonObject callInfo);

    // GPIO Signals TODO: Doku-Description
    void gpioDevicesChanged(const QList<s_IODevices>& deviceList);
    void gpioRoutesChanged(const QList<s_gpioRoute>& routes);
    void gpioRoutesTableChanged(const s_gpioPortList& portList);
    void gpioStateChanged(const QMap<QString, bool> changedGpios);

private:
    EpConfig epCfg;
    PJEndpoint *m_pjEp;
    TransportConfig tCfg;
    pj_pool_t *pool;
    QString TransportProtocol;      // only used for loading a setting  -> is there a better way?
    quint16 m_websocketPort;

    Accounts* m_Accounts;
    AudioRouter* m_AudioRouter;
    Buddies* m_Buddies;
    Codecs* m_Codecs;
    GpioDeviceManager* m_GpioDeviceManager;
    Log* m_Log;
    MessageManager* m_MessageManager;
    Settings* m_Settings;
    Websocket* m_Websocket;

    friend class Accounts;
    friend class AudioRouter;
    friend class Buddies;
    friend class Codecs;
    friend class GpioDevice;
    friend class GpioDeviceManager;
    friend class Log;
    friend class MessageManager;
    friend class Settings;
    friend class PJEndpoint;
    friend class PJAccount;
    friend class PJBuddy;
    friend class PJCall;
    friend class Websocket;
};

#endif // AWAHSIPLIB_H
