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

#include "accounts.h"
#include "awahsiplib.h"
#include "gpiodevicemanager.h"

#define THIS_FILE		"accounts.cpp"


Accounts::Accounts(AWAHSipLib *parentLib, QObject *parent) : QObject(parent), m_lib(parentLib)
{
    m_CallInspectorTimer = new QTimer(this);
    m_CallInspectorTimer->setInterval(1000);
    connect(m_CallInspectorTimer, SIGNAL(timeout()), this, SLOT(CallInspector()));
    connect(this, &Accounts::signalSipStatus, this, &Accounts::OnsignalSipStatus);
}

void Accounts::createAccount(QString accountName, QString server, QString user, QString password, QString filePlayPath, QString fileRecPath, bool fixedJitterBuffer, uint fixedJitterBufferValue, QList<s_callHistory> history , QString uid)
{
    QString idUri = "\""+ accountName +"\" <sip:"+ user +"@"+ server +">";
    QString registrarUri = "sip:"+ server;
    s_account newAccount;

    if(uid.isEmpty())
        uid = createNewUID();

    try{
        // Configure an AccountConfig
        aCfg = defaultACfg;
        aCfg.idUri = idUri.toStdString();
        aCfg.regConfig.registrarUri = registrarUri.toStdString();
        aCfg.ipChangeConfig.hangupCalls = false;
        aCfg.ipChangeConfig.reinviteFlags = PJSUA_CALL_UPDATE_CONTACT & PJSUA_CALL_REINIT_MEDIA & PJSUA_CALL_UPDATE_VIA  & PJSUA_CALL_UPDATE_TARGET ;
        AuthCredInfo cred("digest", "*", user.toStdString(), 0, password.toStdString());
        aCfg.sipConfig.authCreds.push_back(cred);
        PJAccount *account = new PJAccount(m_lib,this);
        account->create(aCfg);
        m_lib->m_Log->writeLog(3,"Account creation successful");
        newAccount.name = accountName;
        newAccount.user = user;
        newAccount.password = password;
        newAccount.serverURI = server;
        newAccount.uid = uid;
        newAccount.accountPtr = account;
        newAccount.AccID = account->getId();
        newAccount.FilePlayPath = filePlayPath;
        newAccount.FileRecordPath = fileRecPath;
        newAccount.fixedJitterBuffer = fixedJitterBuffer;
        newAccount.fixedJitterBufferValue = fixedJitterBufferValue;
        newAccount.CallHistory = history;
        PJSUA2_CHECK_EXPR(m_lib->m_AudioRouter->addSplittComb(newAccount));
        newAccount.gpioDev = GpioDeviceManager::instance()->create(newAccount);
        m_accounts.append(newAccount);
        m_lib->m_Settings->saveAccConfig();
        m_lib->m_AudioRouter->conferenceBridgeChanged();
        emit AccountsChanged(&m_accounts);
    }
    catch(Error& err){
        m_lib->m_Log->writeLog(0,(QString("CreateAccount: Account creation failed") + err.info().c_str()));
    }
}

void Accounts::modifyAccount(int index, QString accountName, QString server, QString user, QString password, QString filePlayPath, QString fileRecPath,bool fixedJitterBuffer, uint fixedJitterBufferValue){
    QString idUri = "\""+ accountName +"\" <sip:"+ user +"@"+ server +">";
    QString registrarUri = "sip:"+ server;
    s_account TmpAccount = m_accounts.at(index);
    aCfg = defaultACfg;
    aCfg.idUri = idUri.toStdString();
    aCfg.regConfig.registrarUri = registrarUri.toStdString();
    AuthCredInfo cred("digest", "*", user.toStdString(), 0, password.toStdString());
    aCfg.sipConfig.authCreds.clear();
    aCfg.sipConfig.authCreds.push_back(cred);
    if(m_accounts.count()> index){
        try{
           m_accounts.at(index).accountPtr->modify(aCfg);
           TmpAccount.name = accountName;
           TmpAccount.serverURI = server;
           TmpAccount.user = user;
           TmpAccount.password = password;
           TmpAccount.uid = m_accounts.at(index).uid;
           TmpAccount.accountPtr = m_accounts.at(index).accountPtr;
           TmpAccount.AccID = m_accounts.at(index).accountPtr->getId();
           TmpAccount.FilePlayPath = filePlayPath;
           TmpAccount.FileRecordPath = fileRecPath;
           TmpAccount.fixedJitterBuffer = fixedJitterBuffer;
           TmpAccount.fixedJitterBufferValue = fixedJitterBufferValue;
           m_accounts.replace(index, TmpAccount);
           m_lib->m_Settings->saveAccConfig();
           m_lib->m_AudioRouter->conferenceBridgeChanged();
           emit AccountsChanged(&m_accounts);
        }
        catch (Error &err){
            m_lib->m_Log->writeLog(0,(QString("ModifyAccount: failed") + err.info().c_str()));
        }
    }
}



void Accounts::removeAccount(int index)
{
    if(m_accounts.count()> index){
        m_accounts.at(index).accountPtr->shutdown();
        m_lib->m_AudioRouter->removeAllRoutesFromAccount(m_accounts.at(index));
        // TODO: here we have to remove the Splittercombiner from the Account!
        GpioDeviceManager::instance()->removeDevice(m_accounts.at(index).uid);
        m_accounts.removeAt(index);
        m_lib->m_AudioRouter->conferenceBridgeChanged();
        m_lib->m_Settings->saveAccConfig();
        emit AccountsChanged(&m_accounts);
    }
}

s_account* Accounts::getAccountByID(int ID){
    for(auto& account : m_accounts){
        if(account.AccID == ID){
            return &account;
        }
    }
    return nullptr;
}

s_account* Accounts::getAccountByUID(QString uid) {
    for(auto& account : m_accounts){
        if(account.uid == uid){
            return &account;
        }
    }
    return nullptr;
}

void Accounts::makeCall(QString number, int AccID)
{
    s_account* account = getAccountByID(AccID);
    QString fulladdr;
    PJCall* newCall;
    fulladdr = "sip:"+ number+"@"+ account->serverURI;
    if(account){
        newCall = new PJCall(this, m_lib, m_lib->m_MessageManager, *account->accountPtr);
        CallOpParam prm(true);        // Use default call settings
        try{
            m_lib->m_Log->writeLog(3,(QString("MakeCall: Trying to call: ") +number ));
            newCall->makeCall(fulladdr.toStdString(), prm);
            account->CallList.append(newCall);
            emit AccountsChanged(getAccounts());
        }
        catch(Error& err){
            m_lib->m_Log->writeLog(1,QString("MakeCall: Call could not be made ") + err.info().c_str());
        }
    }
}


void Accounts::acceptCall(int callId, int AccID)
{
    s_account* account = getAccountByID(AccID);
    PJCall* newCall;
    if(account != Q_NULLPTR){
        try{
            newCall = new PJCall(this, m_lib, m_lib->m_MessageManager, *account->accountPtr, callId);
            CallOpParam prm;
            prm.statusCode = PJSIP_SC_OK;
            m_lib->m_Log->writeLog(3,(QString("AcceptCall: Account: ") + account->name + " accepting call with ID: " + QString::number(callId)));
            newCall->answer(prm);
            account->CallList.append(newCall);
            emit AccountsChanged(getAccounts());
        }
        catch(Error& err){
            m_lib->m_Log->writeLog(1,(QString("AcceptCall: Account: ") + account->name + "accepting call failed: " + err.info().c_str()));
        }
    }

}

void Accounts::hangupCall(int callId, int AccID)
{
    s_account* account = getAccountByID(AccID);
    PJCall *m_call = Q_NULLPTR;
    int pos;

    for (pos = 0; pos < account->CallList.count(); pos++){       // Check if callId is valid
        if (account->CallList.at(pos)->getId() == callId){
            m_call = account->CallList.at(pos);
        }
    }

    if(account && m_call != Q_NULLPTR){
        m_lib->m_Log->writeLog(3,(QString("HangupCall: Account: ") + account->name + " hang up call with ID: " + QString::number(callId)));
        try{
            CallInfo ci = m_call->getInfo();
            CallOpParam prm;

            if(ci.lastStatusCode == PJSIP_SC_RINGING){
                prm.statusCode = PJSIP_SC_BUSY_HERE;
            }
            else{
                prm.statusCode = PJSIP_SC_OK;
            }

            if(callId>= 0 && callId < (int)m_lib->epCfg.uaConfig.maxCalls){
                m_call->hangup(prm);                                                                // callobject gets deleted in onCallState callback
            }
            else{
                m_lib->m_Log->writeLog(3, "HangupCall: Hang up call, max. calls bug");                                                   // todo check if this bug exists anymore!
            }
        }
        catch(Error& err){
            m_lib->m_Log->writeLog(1,(QString("HangupCall: Hang up call failed ") + err.info().c_str()));
        }
    }
}

void Accounts::holdCall(int callId, int AccID)                                                 // todo test me!
{
    s_account* account = getAccountByID(AccID);
    PJCall *m_call = Q_NULLPTR;

    for (int pos = 0; pos<account->CallList.count(); pos++){       // Check if callId is valid
        if (account->CallList.at(pos)->getId() == callId){
            m_call = account->CallList.at(pos);
        }
    }

    if(account && m_call != Q_NULLPTR){
        try{
            m_lib->m_Log->writeLog(3,(QString("HoldCall: Account: ") + account->name + " hold call with ID: " + QString::number(callId)));
            CallOpParam prm(true);

            if(!m_call->isOnHold()){
                m_lib->m_Log->writeLog(3,(QString("HoldCall: Account: ") + account->name + " set call to hold "));
                m_call->setHoldTo(true);
                prm.statusCode = PJSIP_SC_QUEUED;
                m_call->setHold(prm);
            }
            else{
                m_lib->m_Log->writeLog(3,(QString("HoldCall: Account: ") + account->name + " re-invite call"));
                m_call->setHoldTo(false);
                prm.opt.flag = PJSUA_CALL_UNHOLD;
                m_call->reinvite(prm);
            }
        }
        catch(Error& err){
            m_lib->m_Log->writeLog(1,(QString("HoldCall: Hold call failed ") + err.info().c_str()));
        }
    }
}


void Accounts::transferCall(int callId, int AccID, QString destination)
{
    s_account* account = getAccountByID(AccID);
    PJCall *m_call = Q_NULLPTR;

    for (int pos = 0; pos<account->CallList.count(); pos++){       // Check if callId is valid
            if (account->CallList.at(pos)->getId() == callId){
                m_call = account->CallList.at(pos);
            }
        }

    if(account && m_call != Q_NULLPTR){
        try{
            m_lib->m_Log->writeLog(3,(QString("TransferCall: Account: ") + account->name + " transfer call to " + destination));
            CallOpParam prm;
            prm.statusCode = PJSIP_SC_CALL_BEING_FORWARDED;
            m_call->xfer(destination.toStdString(), prm);
        }
        catch(Error& err){
            m_lib->m_Log->writeLog(1,(QString("TransferCall: Transfering call failed ") + err.info().c_str()));
        }
    }
}

QJsonObject Accounts::getCallInfo(int callId, int AccID){
    QJsonObject callInfo;
    QStringList Line;
    s_account* account = getAccountByID(AccID);
    PJCall *m_call = Q_NULLPTR;
    StreamInfo streaminfo;
    StreamStat streamstats;
    pjsua_call_info ci;

    for (int pos = 0; pos<account->CallList.count(); pos++){       // Check if callId is valid
        if (account->CallList.at(pos)->getId() == callId){
            m_call = account->CallList.at(pos);
            pjsua_call_get_info(callId, &ci);
            if(ci.media_status ==PJSUA_CALL_MEDIA_ACTIVE ){
                streaminfo = account->CallList.at(pos)->getStreamInfo(0);
                callInfo["Codec: "] = QString::fromStdString(streaminfo.codecName);
                callInfo["Channel Count: "] = (int)streaminfo.audCodecParam.info.channelCnt;
                callInfo["Frame lenght: "] = (int)streaminfo.audCodecParam.info.frameLen;
                callInfo["Frames per package: "] = (int)streaminfo.audCodecParam.setting.frmPerPkt;
                callInfo["Clock rate: "] = (int)streaminfo.codecClockRate;
                callInfo["Rx Payload type: "] = (int)streaminfo.rxPt;
                callInfo["Tx Payload type: "] = (int)streaminfo.txPt;
                callInfo["Bit depth: "] = (int)streaminfo.audCodecParam.info.pcmBitsPerSample;

                streamstats = account->CallList.at(pos)->getStreamStat(0);
                callInfo["RTCP: SDES: "] = QString("%1, %2, %3, %4, %5, %6, %7").arg(
                        QString::fromStdString(streamstats.rtcp.peerSdes.cname),
                        QString::fromStdString(streamstats.rtcp.peerSdes.name),
                        QString::fromStdString(streamstats.rtcp.peerSdes.email),
                        QString::fromStdString(streamstats.rtcp.peerSdes.phone),
                        QString::fromStdString(streamstats.rtcp.peerSdes.loc),
                        QString::fromStdString(streamstats.rtcp.peerSdes.tool),
                        QString::fromStdString(streamstats.rtcp.peerSdes.note));
                callInfo["JB: FrameSize (bytes): "] = (int)streamstats.jbuf.frameSize;
                callInfo["JB: Minimum allowed prefetch (frms): "] = (int)streamstats.jbuf.minPrefetch;
                callInfo["JB: Maximum allowed prefetch (frms): "] = (int)streamstats.jbuf.maxPrefetch;
                callInfo["JB: Current burst level (frms): "] = (int)streamstats.jbuf.burst;
                callInfo["JB: Current prefetch value (frms): "] = (int)streamstats.jbuf.prefetch;
                callInfo["JB: Current buffer size (frms): "] = (int)streamstats.jbuf.size;
                callInfo["JB: Average delay (ms): "] = (int)streamstats.jbuf.avgDelayMsec;
                callInfo["JB: Minimum delay (ms): "] = (int)streamstats.jbuf.minDelayMsec;
                callInfo["JB: Maximum delay (ms): "] = (int)streamstats.jbuf.maxDelayMsec;
                callInfo["JB: Deviation of delay (ms): "] = (int)streamstats.jbuf.devDelayMsec;
                callInfo["JB: Average burst (frms): "] = (int)streamstats.jbuf.avgBurst;
                callInfo["JB: Lost (frms): "] = (int)streamstats.jbuf.lost;
                callInfo["JB: Discarded (frms): "] = (int)streamstats.jbuf.discard;
                callInfo["JB: Number of empty on GET events: "] = (int)streamstats.jbuf.empty;
             }
        }
    }

    if(m_call !=Q_NULLPTR){
        try{
            QString calldump = QString::fromStdString(m_call->dump(true,""));
            Line = calldump.split("\n");

            int startPos = Line.at(0).indexOf("To:") + 4 ;
            int endPos = Line.at(0).indexOf(";tag=");
            int length = endPos - startPos;
            callInfo["Status:"] = Line.at(0).left(startPos - 4);
            callInfo["Connected to:"] = Line.at(0).mid(startPos, length);

            if(Line.count()>15){
                callInfo["Call time:"] = Line.at(1).mid(13,11);
                callInfo["Peer:"] = Line.at(2).mid(Line.at(2).indexOf("peer=")+5);
                callInfo["SRTP status:"] = Line.at(3).mid(18);
                startPos = Line.at(5).indexOf("@avg=");
                callInfo["Recieved packets (total):"] = Line.at(5).mid(14,startPos-15);
                callInfo["Recieved packets (average):"] = Line.at(5).mid(startPos+5);
                startPos = Line.at(6).indexOf("discrd=");
                callInfo["Recieved packets lost:"] = Line.at(6).mid(17,startPos -19);
                endPos = Line.at(6).indexOf("dup=");
                callInfo["Recieved packets discarded:"] = Line.at(6).mid(startPos+7,endPos-startPos-9);
                startPos = Line.at(6).indexOf("reord=");
                callInfo["Recieved packets reordered:"] = Line.at(6).mid(startPos+ 6);
                callInfo["RX loss period min:"] = Line.at(8).mid(21,8) + " msec";
                callInfo["RX loss period average:"] = Line.at(8).mid(29,8) + " msec";
                callInfo["RX loss period max:"] = Line.at(8).mid(37,8) + " msec";
                callInfo["RX loss period last:"] = Line.at(8).mid(45,8) + " msec";
                callInfo["RX loss period dev:"] = Line.at(8).mid(53,8) + " msec";
                callInfo["RX jitter min:"] = Line.at(9).mid(21,8) + " msec";
                callInfo["RX jitter average:"] = Line.at(9).mid(29,8) + " msec";
                callInfo["RX jitter max:"] = Line.at(9).mid(37,8) + " msec";
                callInfo["RX jitter last:"] = Line.at(9).mid(45,8) + " msec";
                callInfo["RX jitter dev:"] = Line.at(9).mid(53,8) + " msec";

                startPos = Line.at(11).indexOf("@avg=");
                callInfo["Transmitted packets (total):"] = Line.at(11).mid(14,startPos-15);
                callInfo["Transmitted packets (average):"] = Line.at(11).mid(startPos+5);
                startPos = Line.at(12).indexOf("loss=");
                endPos = Line.at(12).indexOf("dup=");
                callInfo["Transmitted packets lost:"] = Line.at(12).mid(startPos+5,endPos - startPos-7);
                startPos = Line.at(12).indexOf("reorder=");
                callInfo["Transmitted packets reordered:"] = Line.at(12).mid(startPos+8);
                callInfo["TX loss period min:"] = Line.at(14).mid(21,8) + " msec";
                callInfo["TX loss period average:"] = Line.at(14).mid(29,8) + " msec";
                callInfo["TX loss period max:"] = Line.at(14).mid(37,8) + " msec";
                callInfo["TX loss period last:"] = Line.at(14).mid(45,8) + " msec";
                callInfo["TX loss period dev:"] = Line.at(14).mid(53,8) + " msec";
                callInfo["TX jitter min:"] = Line.at(15).mid(21,8) + " msec";
                callInfo["TX jitter average:"] = Line.at(15).mid(29,8) + " msec";
                callInfo["TX jitter max:"] = Line.at(15).mid(37,8) + " msec";
                callInfo["TX jitter last:"] = Line.at(15).mid(45,8) + " msec";
                callInfo["TX jitter dev:"] = Line.at(15).mid(53,8) + " msec";
                callInfo["RTT min:"] = Line.at(16).mid(21,8) + " msec";
                callInfo["RTT average:"] = Line.at(16).mid(29,8) + " msec";
                callInfo["RTT max:"] = Line.at(16).mid(37,8) + " msec";
                callInfo["RTT last:"] = Line.at(16).mid(45,8) + " msec";
                callInfo["RTT dev:"] = Line.at(16).mid(53,8) + " msec";
            }
        }
        catch(Error& err){
            callInfo["Error: "] = err.info().c_str();
            m_lib->m_Log->writeLog(1,(QString("dumpCall: Call-Dump failed ") + err.info().c_str()));
        }
    }
    return callInfo;
}


void Accounts::addCallToHistory(int AccID, QString callUri, int duration, QString codec, bool outgoing)
{
    s_account* account = getAccountByID(AccID);
    bool entryexists = false;
    for(auto histentry : account->CallHistory){
        if(histentry.callUri == callUri){
            entryexists = true;
            histentry.codec = codec;
            histentry.count++;
            histentry.duration = duration;
            histentry.outgoing = outgoing;
            for(int i =0; i<account->CallHistory.size(); i++){      // deleate the existing entry and place then modified entry as the first item
                if(account->CallHistory.at(i).callUri == callUri){
                    account->CallHistory.removeAt(i);
                    account->CallHistory.prepend(histentry);
                }
            }
        }
    }

    if(!entryexists){
        s_callHistory newCall;
        newCall.callUri = callUri;
        newCall.codec = codec;
        newCall.duration = duration;
        newCall.outgoing = outgoing;
        newCall.count = 1;
        if(account->CallHistory.count()>9){
            account->CallHistory.removeLast();
        }
        account->CallHistory.prepend(newCall);
    }
    m_lib->m_Settings->saveAccConfig();
}

const QList<s_callHistory>* Accounts::getCallHistory(int AccID){
    s_account* account = getAccountByID(AccID);
    return &account->CallHistory ;
}


void Accounts::OncallStateChanged(int accID, int role, int callId, bool remoteofferer, long calldur, int state, int lastStatusCode, QString statustxt, QString remoteUri)
{
    emit callStateChanged(accID, role, callId, remoteofferer, calldur, state, lastStatusCode, statustxt, remoteUri);
    s_account* thisAccount = getAccountByID(accID);
    thisAccount->CallStatusCode = state;
    thisAccount->CallStatusText = statustxt;
    thisAccount->ConnectedTo = remoteUri;
    m_lib->m_Log->writeLog(3,(QString("Callstate of: ") + remoteUri + " is:  " + statustxt));
    if(state == PJSIP_INV_STATE_EARLY && lastStatusCode == 180){
        if(role == 1){                                                                           // autoanswer call
            acceptCall(accID,callId);
        }
    }
    else if(state == PJSIP_INV_STATE_CONFIRMED && lastStatusCode == 200){
        thisAccount->gpioDev->setConnected(true);
        emit m_lib->m_AudioRouter->audioRoutesChanged(m_lib->m_AudioRouter->getAudioRoutes());
    }
    else if(state == PJSIP_INV_STATE_DISCONNECTED) {
        thisAccount->gpioDev->setConnected(false);
        //         if(sipStatus.fields.SipClientRegistered)
        {
            //             pjsua->sendPresenceStatus(online);
        }
    }
}

void Accounts::OnsignalSipStatus(int accId, int status, QString remoteUri)
{
    for(auto& account : m_accounts){
        if(account.AccID == accId){
            account.SIPStatusCode = status;
            account.SIPStatusText = remoteUri;
        }
    }
}

void Accounts::startCallInspector()
{
    m_CallInspectorTimer->start();
}

void Accounts::CallInspector()
{
    QJsonObject m_callInfo;
    for(auto& account : m_accounts){                    // send callInfo for every call one a second
        for(auto& call : account.CallList ){
            m_callInfo = getCallInfo(call->getId(),account.AccID);
            emit callInfo(account.AccID, call->getId(),m_callInfo);

            if(m_MaxCallTime){                                              // hang up calls if call time is exeeded
                QTime time = QTime::fromString(m_callInfo["Call time:"].toString(), "HH'h':mm'm':ss's'");
                if(m_MaxCallTime <= (time.hour()*60 + time.minute())){
                    hangupCall(call->getId(),account.AccID);
                    m_lib->m_Log->writeLog(3,(QString("Max call time exeeded on account ID: ")+ QString::number(account.AccID) + " call disconnected"));
                }
            }
        }
    }


}
