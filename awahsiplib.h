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

#include <QJsonDocument>

#define AWAHSIP_VERSION 3.2
#ifndef GITHUBBUILD
#define BUILD_NO "3.2.5"
#endif

class AWAHSipLib : public QObject
{
    Q_OBJECT
public:
    ~AWAHSipLib();
    static AWAHSipLib* instance(QObject *parent = nullptr);

    static void prepareLib();

    // Public API - Accounts
    void createAccount(QString accountName, QString server, QString user, QString password, QString filePlayPath, QString fileRecPath, bool fileRecordRXonly, bool fixedJitterBuffer, uint fixedJitterBufferValue, QString autoconnectToBuddyUID, bool autoconnectEnable, bool hasDTMFGPIO) const
        { return m_Accounts->createAccount(accountName, server, user, password, filePlayPath, fileRecPath, fileRecordRXonly ,fixedJitterBuffer, fixedJitterBufferValue, autoconnectToBuddyUID, autoconnectEnable, hasDTMFGPIO); };
    void modifyAccount(QString uid, QString accountName, QString server, QString user, QString password, QString filePlayPath, QString fileRecPath, bool fileRecordRXonly, bool fixedJitterBuffer, uint fixedJitterBufferValue, QString autoconnectToBuddyUID, bool autoconnectEnable, bool hasDTMFGPIO) const
        { return m_Accounts->modifyAccount(uid, accountName, server, user, password, filePlayPath, fileRecPath, fileRecordRXonly, fixedJitterBuffer, fixedJitterBufferValue, autoconnectToBuddyUID, autoconnectEnable, hasDTMFGPIO); };
    void removeAccount(QString uid) const { return m_Accounts->removeAccount(uid); };
    QList <s_account>* getAccounts() const { return m_Accounts->getAccounts(); };
    void makeCall(QString number, int AccID,s_codec codec) const { return m_Accounts->makeCall(number, AccID, codec); };
    void hangupCall(int callId, int AccID) const { return m_Accounts->hangupCall(callId, AccID); };
    void acceptCall(int callId, int AccID) const { return m_Accounts->acceptCall(callId, AccID); };
    void holdCall(int callId, int AccID) const { return m_Accounts->holdCall(callId, AccID); };
    void transferCall(int callId, int AccID, QString destination) const { return m_Accounts->transferCall(callId, AccID, destination); };
    void sendDTMFtoAllCalls(QString Uid, char DTMFdigit) const {return m_Accounts->sendDTMFtoAllCalls(Uid, DTMFdigit); };
    QJsonObject getCallInfo(int callID, int AccID) const { return m_Accounts->getCallInfo(callID, AccID); };
    QString getSDP(int callId, int AccID) const { return m_Accounts->getSDP(callId, AccID); };
    const QList<s_callHistory>* getCallHistory(int AccID) const { return m_Accounts->getCallHistory(AccID); };
    const s_account* getAccountByID(int ID) {return m_Accounts->getAccountByID(ID); };

    QList<s_IODevices>& getIoDevices();

    // Public API - AudioRouter
    QList <s_audioRoutes> getAudioRoutes() const { return m_AudioRouter->getAudioRoutes(); };
    QStringList listInputSoundDev() const { return m_AudioRouter->listInputSoundDev(); };
    QStringList listOutputSoundDev() const { return m_AudioRouter->listOutputSoundDev(); };
    void addAudioDevice(int recordDevId, int playbackDevId) const { return m_AudioRouter->addAudioDevice(recordDevId, playbackDevId); };
    void removeAudioDevice(QString uid) const { return m_AudioRouter->removeAudioDevice(uid); };
    void addFilePlayer(QString Name, QString File) const { return m_AudioRouter->addFilePlayer(Name, File); };
    void addFileRecorder(QString File) const { return m_AudioRouter->addFileRecorder(File); };
    const s_audioPortList& getConfPortsList() const { return m_AudioRouter->getConfPortsList(); };
    int connectConfPort(int src_slot, int sink_slot, int level, bool persistant = true) const
        { return m_AudioRouter->connectConfPort(src_slot, sink_slot, level, persistant); };
    int disconnectConfPort(int src_slot, int sink_slot) const { return m_AudioRouter->disconnectConfPort(src_slot, sink_slot); };
    void changeConfPortLevel(int src_slot, int sink_slot, int level) const { return m_AudioRouter->changeConfPortLevel(src_slot, sink_slot, level); };
    void addToneGen(int freq) const { return m_AudioRouter->addToneGen(freq); };
    QList<s_IODevices>& getAudioDevices() const { return *m_AudioRouter->getAudioDevices(); };
    int getSoundDevID(QString DeviceName) const { return m_AudioRouter->getSoundDevID(DeviceName); };
    void changeConfportsrcName(const QString portName, const QString customName) const { return m_AudioRouter->changeConfportsrcName(portName, customName); };
    void changeConfportdstName(const QString portName, const QString customName) const { return m_AudioRouter->changeConfportdstName(portName, customName); };

    // Public API - Buddies
    void addBuddy(QString buddyUrl, QString name, QString accUid, QJsonObject codec) const { return m_Buddies->addBuddy(buddyUrl, name, accUid, codec); };
    void editBuddy(QString buddyUrl, QString name, QString accUid, QJsonObject codec, QString uid) const { return m_Buddies->editBuddy(buddyUrl, name, accUid, codec, uid); };
    void removeBuddy(QString uid) const { return  m_Buddies->removeBuddy(uid); };
    QList<s_buddy>* getBuddies() const { return m_Buddies->getBuddies(); };

    // Public API - Codecs
    QList<s_codec> getActiveCodecs() const { return m_Codecs->getActiveCodecs(); };

    // Public API - GpioDeviceManager
    const QString createGpioDev(QJsonObject &newDev) { return m_GpioDeviceManager->createGpioDev(newDev); };
    void removeGpioDevice(QString uid) { return m_GpioDeviceManager->removeDevice(uid); };
    const QList<s_IODevices>& getGpioDevices() const { return m_GpioDeviceManager->getGpioDevices(); };
    const QJsonObject getGpioDevTypes() const { return m_GpioDeviceManager->getGpioDevTypes(); };
    void setGPIStateOfDevice(QString Deviceuid, uint number, bool state) {return m_GpioDeviceManager->setGPIStateOfDevice(Deviceuid, number, state); };

    // Public API - GpioRouter
    const QList<s_gpioRoute>& getGpioRoutes() { return GpioRouter::instance()->getGpioRoutes(); };
    const s_gpioPortList& getGpioPortsList() { return GpioRouter::instance()->getGpioPortsList(); };
    void connectGpioPort(QString srcSlotId, QString destSlotId, bool inverted, bool persistant = true)
        { return GpioRouter::instance()->connectGpioPort(srcSlotId, destSlotId, inverted, persistant); };
    void disconnectGpioPort(QString srcSlotId, QString destSlotId) { return GpioRouter::instance()->disconnectGpioPort(srcSlotId, destSlotId); };
    void changeGpioCrosspoint(QString srcSlotId, QString destSlotId, bool inverted)
        { return GpioRouter::instance()->changeGpioCrosspoint(srcSlotId, destSlotId, inverted); };
    const QMap<QString, bool> getGpioStates() { return GpioRouter::instance()->getGpioStates(); };


    // Public API - Log
    QStringList readNewestLog() const { return m_Log->readNewestLog();};

    // Public API - MessageManager
    void sendDtmf(int callId, int AccID, QString num) const { return m_MessageManager->sendDtmf(callId, AccID, num); };

    // Public API - Settings
    const QJsonObject* getSettings() const { return m_Settings->getSettings(); };
    void setSettings(QJsonObject editedSetting) { return m_Settings->setSettings(editedSetting); } ;
    const QJsonObject getCodecPriorities() const { return m_Settings->getCodecPriorities(); };
    void setSCodecPriorities(QJsonObject Codecpriority) { return m_Settings->setCodecPriorities(Codecpriority); } ;

    /**
    * @brief get the Version of AWAH Sip and underliying PJSIP Version
    * @return JSON object with all the Versions
    */
     QJsonObject getVersions();

public slots:
    void slotSendMessage(int callId, int AccID, QString type, QByteArray message);
    void slotIoDevicesChanged(QList<s_IODevices>& IoDev);

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
    void signalBuddyStatus(QString buddyUri, int status);

    /**
    * @brief signalBuddyEntryChanged is emmited when a buddy is added, edited or removed from the buddies list
    * @param the updated buddy list with all buddies
    */
    void signalBuddyEntryChanged(QList<s_buddy>* buddies);

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
    * @brief Signal if a level of a confport has changed
    * @param audioRoutes the Route with the changed level
    */
    void confportLevelChanged(s_audioRoutes changedRoute);

    /**
    * @brief Signal if audio route Table from the conference-bridge changed
    * @param portList with all sources and sinks as struct
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
    void AudioDevicesChanged(QList<s_IODevices>& audioDev);

    /**
    * @brief Signal a Callinfo updated every second for every active call
    * @param accId the account identifier of the Call
    * @param CallId the call identifier
    * @param callInfo a JsonObject with various info parameters
    */
    void callInfo(int accId, int callId, QJsonObject callInfo);

    /**
    * @brief Signal if device config changed
    * @param QList of the new device config
    */
    void IoDevicesChanged(QList<s_IODevices>& IoDev);

    /**
    * @brief Signal if GPIO device config changed
    * @param QList of the new device config
    */
    void gpioDevicesChanged(const QList<s_IODevices>& deviceList);

    /**
    * @brief Signal if GPIO Routes changed
    * @param QList with the new routes
    */
    void gpioRoutesChanged(const QList<s_gpioRoute>& routes);

    /**
    * @brief Signal if GPIO routes table has changed (e.g. a device is gone offline)
    * @param QList of the new portlist
    */
    void gpioRoutesTableChanged(const s_gpioPortList& portList);

    /**
    * @brief Signal if a GPIO state has changed
    * @param QMap with the changed GPIOS
    */
    void gpioStateChanged(const QMap<QString, bool> changedGpios);

private:
    explicit AWAHSipLib(QObject *parent = nullptr);

    static AWAHSipLib *AWAHSipLibInstance;
    EpConfig epCfg;
    PJEndpoint *m_pjEp;
    TransportConfig tCfg;
    pj_pool_t *pool;
    QString TransportProtocol;      // only used for loading a setting  -> is there a better way?
    quint16 m_websocketPort;
    QList<s_IODevices> m_IoDevices;

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
    friend class AudioCrosspointDev;
    friend class libgpiod_Device;
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
