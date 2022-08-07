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

#ifndef ACCOUNTS_H
#define ACCOUNTS_H

#include <QObject>
#include "types.h"
#include <QTimer>

class AWAHSipLib;

class Accounts : public QObject
{
    Q_OBJECT
public:
    explicit Accounts(AWAHSipLib *parentLib, QObject *parent = nullptr);

    /**
    * @brief start the call inspector, to get call information once a second
    *        the call inspector also detects media loss, handles max call time and
    *        is redials calls presistent calls
    */
    void startCallInspector();

    /**
    * @brief create and regiser a new SIP account
    * @param accountName a user definable name to identify the account
    * @param server the uri of the sip server
    * @param user  username of the account
    * @param password the password
    * @param FilePlayPath if a path is specified then this file will be payed on call accepted
    * @param FileRecPath if a record path is specified all calls will be recorded into this directory
    * @param fileRecordRXonly if true only recieved (far end) audio will be recorded instead of recieved and transmitted signal
    * @param fixedJitterBuffer if true a fixed jitter buffer is set, otherwise an adaptive jitter buffer is active
    * @param fixedJitterBufferValue the time in ms for the fixed jitter buffer
    * @param autoconnectToBuddyUID connect to this buddy automatically as soon the buddy is online
    * @param autoconnectEnable if true autoconnect is activated (used to terminate autoconnect calls)
    * @param history the call history
    * @param uid the unique idenifyer of the account
    */
    void createAccount(QString accountName, QString server, QString user, QString password, QString filePlayPath, QString fileRecPath, bool fileRecordRXonly, bool fixedJitterBuffer, uint fixedJitterBufferValue, QString autoconnectToBuddyUID = "", bool autoconnectEnable = true ,QList<s_callHistory> history = QList<s_callHistory>(), QString uid = "");

    /**
    * @brief modify a existing SIP account
    * @param uid of the account you wish to modify
    * @param accountName a user definable name to identify the account
    * @param server the uri of the sip server
    * @param user  username of the account
    * @param password if this field is empty the password stays the same. You only have to provide a password if you like to change it
    * @param FilePlayPath if a path is specified then this file will be payed on call accepted
    * @param FileRecPath if a record path is specified all calls will be recorded into this directory
    * @param fileRecordRXonly if true only recieved (far end) audio will be recorded instead of recieved and transmitted signal
    * @param fixedJitterBuffer if true a fixed jitter buffer is set, otherwise an adaptive jitter buffer is active
    * @param fixedJitterBufferValue the time in ms for the fixed jitter buffer
    * @param autoconnectToBuddyUID connect to this buddy automatically as soon the buddy is online
    ** @param autoconnectEnable if true autoconnect is activated (used to terminate autoconnect calls)
    */
    void modifyAccount(QString uid, QString accountName, QString server, QString user, QString password, QString filePlayPath, QString fileRecPath, bool fileRecordRXonly, bool fixedJitterBuffer, uint fixedJitterBufferValue, QString autoconnectToBuddyUID = "", bool autoconnectEnable = true);

    /**
    * @brief remove a SIP Account
    * @param the UID of the account you like to remove
    */
    void removeAccount(QString uid);

    /**
    * @brief get the active accounts
    * @return the Account struct
    */
    QList <s_account>* getAccounts() { return &m_accounts; };

    /**
    * @brief get an account by it's ID
    * @return the Account struct
    */
    s_account* getAccountByID(int ID);

    /**
    * @brief get an account by it's uid
    * @return the Account struct
    */
    s_account* getAccountByUID(QString uid);

    /**
    * @brief establish a new call
    * @param number the number you like to call
    * @param AccID the account that originates the call
    * @param codec the codec and optional parameters of that codec
    */
    void makeCall(QString number, int AccID, s_codec codec);

    /**
    * @brief end a call
    * @param callId the ID of the call you like to end
    * @param AccID the account that owns the call
    */
    void hangupCall(int callId, int AccID);

    /**
    * @brief accept a call
    * @param callId the ID of the call you like to accept
    * @param AccID the account that owns the call
    */
    void acceptCall(int callId, int AccID);

    // untestet and thus not documented yet
    void holdCall(int callId, int AccID);
    void transferCall(int callId, int AccID, QString destination);

    void sendDTMFtoAllCalls(QString Uid, char DTMFdigit);

    /**
    * @brief get streaminfo and callstatistics for a call
    * @param callId the ID of the call you're intrested in
    * @param AccID the account that owns the call
    * @return QJsonObject with all the infos and their parameters
    */
    QJsonObject getCallInfo(int callId, int AccID);

    /**
    * @brief add call to the challhistory (the last 10 calls will be stored)
    * @param AccID the account witch history shold be edited
    * @param callUri the Uri of the call
    * @param duration the duration of the call
    * @param codec the used codec
    * @param outgoing 1 = the call was outgoing (we called someone)
    */
    void addCallToHistory(int AccID, QString callUri, int duration, s_codec codec, bool outgoing);

    /**
    * @brief get streaminfos callhistory for an account
    * @param AccID the account
    * @return a qlist with up to 10 calls
    */
    const QList<s_callHistory>* getCallHistory(int AccID);

    /**
    * @brief Set or modify accounts presence status to be advertised to remote/ presence soubscribers
    * @param AccID the account
    * @return the presence state (e.g. online, busy...)
    */
    void sendPresenceStatus(int AccID,AWAHPresenceState AWAHpresence);

    void OncallStateChanged(int accID, int role, int callId, bool remoteofferer, long calldur, int state, int lastStatusCode, QString statustxt, QString remoteUri);
    void OnsignalSipStatus(int accId, int status, QString remoteUri);

    AccountConfig getDefaultACfg() const { return defaultACfg;};

    void setDefaultACfg(const AccountConfig &value){ defaultACfg = value; };

    int m_MaxCallTime;
    int m_CallDisconnectRXTimeout;

signals:
    void regStateChanged(int accId, bool status);
    void callStateChanged(int accId, int role, int callId, bool remoteofferer, long calldur, int state, int lastStatusCode, QString statustxt, QString remoteUri);
    void signalSipStatus(int accId, int status, QString remoteUri);
    void callInfo(int accId, int callId, QJsonObject callInfo);
    void AccountsChanged(QList <s_account>* Accounts);

private:
    AWAHSipLib* m_lib;
    AccountConfig aCfg, defaultACfg;
    QTimer *m_CallInspectorTimer;



    /**
    * @brief All accounts are added to this list
    *       in order to save and load current setup
    */
    QList<s_account> m_accounts;

private slots:
    void CallInspector();


};

#endif // ACCOUNTS_H
