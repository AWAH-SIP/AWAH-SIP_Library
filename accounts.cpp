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

void Accounts::makeCall(QString number, int AccID, s_codec codec)
{
    s_account* account = getAccountByID(AccID);
    QString fulladdr;
    PJCall* newCall;
    fulladdr = "sip:"+ number+"@"+ account->serverURI;
    if(account){
        m_lib->m_Codecs->selectCodec(codec);
        m_lib->m_Codecs->setCodecParam(codec);
        newCall = new PJCall(this, m_lib, m_lib->m_MessageManager, *account->accountPtr);
        CallOpParam prm(true);        // Use default call settings
        try{
            m_lib->m_Log->writeLog(3,(QString("MakeCall: Trying to call: ") +number ));
            newCall->makeCall(fulladdr.toStdString(), prm);
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
            s_Call thisCall(account->splitterSlot);
            thisCall.callptr = newCall;
            thisCall.callId = callId;
            m_lib->m_Log->writeLog(3,(QString("AcceptCall: Account: ") + account->name + " accepting call with ID: " + QString::number(callId)));
            newCall->answer(prm);
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
    PJCall *call = nullptr;
    QMutableListIterator<s_Call> i(account->CallList);
    while(i.hasNext()){
        s_Call &callentry = i.next();                    //check if its a valid
        if(callentry.callId == callId){
            call = callentry.callptr;
            callentry.CallStatusCode = 8;
            emit callStateChanged(account->AccID,0,callentry.callId,0,0,8,8,"trying to hang up",callentry.ConnectedTo);
            break;
        }
    }
    if (call == nullptr)
        return;

    if(account && call != Q_NULLPTR){
        m_lib->m_Log->writeLog(3,(QString("HangupCall: Account: ") + account->name + " hang up call with ID: " + QString::number(callId)));
        try{
            CallInfo ci = call->getInfo();
            CallOpParam prm;

            if(ci.lastStatusCode == PJSIP_SC_RINGING){
                prm.statusCode = PJSIP_SC_BUSY_HERE;
            }
            else{
                prm.statusCode = PJSIP_SC_OK;
            }

            if(callId>= 0 && callId < (int)m_lib->epCfg.uaConfig.maxCalls){
                call->hangup(prm);                                                                // callobject gets deleted in onCallState callback

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
        if (account->CallList.at(pos).callId == callId){
            m_call = account->CallList.at(pos).callptr;
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
        if (account->CallList.at(pos).callId == callId){
            m_call = account->CallList.at(pos).callptr;
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
    PJCall *pjCall = Q_NULLPTR;
    StreamInfo streaminfo;
    StreamStat streamstats;
    MediaTransportInfo transportinfo;
    CallInfo callinfo;
    pjsua_call_info ci;

    for (int pos = 0; pos<account->CallList.count(); pos++){       // Check if callId is valid
        if (account->CallList.at(pos).callId == callId && callId < (int)m_lib->epCfg.uaConfig.maxCalls){
            try {
                pjCall = account->CallList.at(pos).callptr;
                PJSUA2_CHECK_EXPR( pjsua_call_get_info(callId, &ci) );
            }  catch (Error &err) {
                m_lib->m_Log->writeLog(0,(QString("Accounts::getCallInfo() failed") + err.info().c_str()));
            }
            break;          // found!
        }
    }

    if(pjCall !=Q_NULLPTR){
        try{
            if(ci.media_status ==PJSUA_CALL_MEDIA_ACTIVE ){
                callinfo = pjCall->getInfo();
                transportinfo =pjCall->getMedTransportInfo(0);
                streaminfo = pjCall->getStreamInfo(0);
                streamstats = pjCall->getStreamStat(0);
                callInfo["Status:"] = QString::fromStdString(callinfo.stateText);
                callInfo["Connected to:"] = QString::fromStdString(callinfo.remoteUri);
                callInfo["Call time:"] = QDateTime::fromSecsSinceEpoch(callinfo.connectDuration.sec, Qt::OffsetFromUTC).toString("hh:mm:ss");
                callInfo["Peer: "] = QString::fromStdString(transportinfo.srcRtpName);
                callInfo["Recieved data total:"] = sizeFormat(streamstats.rtcp.rxStat.bytes);
                if(callinfo.connectDuration.sec){       //avoid division by zero
                    callInfo["Recieved data average kbps:"] = QString::number(streamstats.rtcp.rxStat.bytes * 8 /1024 / callinfo.connectDuration.sec);
                    callInfo["Transmitted data average kbps:"] = QString::number(streamstats.rtcp.txStat.bytes * 8 /1024 / callinfo.connectDuration.sec);
                }
                callInfo["Recieved packets lost:"] = (int)streamstats.rtcp.rxStat.loss;
                if(streamstats.rtcp.rxStat.pkt){
                    callInfo["Recieved packets lost: (%)"] = (float)(100.0/streamstats.rtcp.rxStat.pkt*streamstats.rtcp.rxStat.loss);
                    callInfo["Transmitted packets lost: (%)"] = (float)(100.0/streamstats.rtcp.txStat.pkt*streamstats.rtcp.txStat.loss);
                }
                callInfo["Recieved packets discarded:"] = (int)streamstats.rtcp.rxStat.discard;
                callInfo["RX loss period min: (ms)"] = (int)streamstats.rtcp.rxStat.lossPeriodUsec.min/1000;
                callInfo["RX loss period average: (ms)"] = (int)streamstats.rtcp.rxStat.lossPeriodUsec.mean/1000;
                callInfo["RX loss period max: (ms)"] = (int)streamstats.rtcp.rxStat.lossPeriodUsec.max/1000;
                callInfo["RX loss period last: (ms)"] = (int)streamstats.rtcp.rxStat.lossPeriodUsec.last/1000;
                callInfo["RX jitter min: (ms)"] = (float)streamstats.rtcp.rxStat.jitterUsec.min/1000;
                callInfo["RX jitter max: (ms)"] = (float)streamstats.rtcp.rxStat.jitterUsec.max/1000;
                callInfo["RX jitter last: (ms)"] = (float)streamstats.rtcp.rxStat.jitterUsec.last/1000;
                callInfo["RX jitter average: (ms)"] = (float)streamstats.rtcp.rxStat.jitterUsec.mean/1000;
                callInfo["Transmitted data total:"] = sizeFormat(streamstats.rtcp.txStat.bytes);
                callInfo["Transmitted packets lost:"] = (int)streamstats.rtcp.txStat.loss;
                callInfo["Transmitted packets discarded:"] = (int)streamstats.rtcp.txStat.discard;
                callInfo["TX loss period min: (ms)"] = (int)streamstats.rtcp.txStat.lossPeriodUsec.min/1000;
                callInfo["TX loss period average: (ms)"] = (int)streamstats.rtcp.txStat.lossPeriodUsec.mean/1000;
                callInfo["TX loss period max: (ms)"] = (int)streamstats.rtcp.txStat.lossPeriodUsec.max/1000;
                callInfo["TX loss period last: (ms)"] = (int)streamstats.rtcp.txStat.lossPeriodUsec.last/1000;
                callInfo["TX jitter min: (ms)"] = (float)streamstats.rtcp.txStat.jitterUsec.min/1000;
                callInfo["TX jitter average: (ms)"] = (float)streamstats.rtcp.txStat.jitterUsec.mean/1000;
                callInfo["TX jitter max: (ms)"] = (float)streamstats.rtcp.txStat.jitterUsec.max/1000;
                callInfo["TX jitter last: (ms)"] = (float)streamstats.rtcp.txStat.jitterUsec.last/1000;
                callInfo["RTT min: (ms)"] = (float)streamstats.rtcp.rttUsec.min/1000;
                callInfo["RTT average: (ms)"] = (float)streamstats.rtcp.rttUsec.mean/1000;
                callInfo["RTT max: (ms)"] = (float)streamstats.rtcp.rttUsec.max/1000;
                callInfo["RTT last: (ms)"] = (float)streamstats.rtcp.rttUsec.last/1000;
                callInfo["Codec:"] = QString::fromStdString(streaminfo.codecName);
                callInfo["Channel Count:"] = (int)streaminfo.audCodecParam.info.channelCnt;
                callInfo["Frame lenght:"] = (int)streaminfo.audCodecParam.info.frameLen;
                callInfo["Frames per package:"] = (int)streaminfo.audCodecParam.setting.frmPerPkt;
                callInfo["Clock rate:"] = (int)streaminfo.codecClockRate;
                callInfo["Rx Payload type:"] = (int)streaminfo.rxPt;
                callInfo["Tx Payload type:"] = (int)streaminfo.txPt;
                callInfo["Bit depth:"] = (int)streaminfo.audCodecParam.info.pcmBitsPerSample;
                callInfo["RTCP: SDES:"] = QString("%1, %2, %3, %4, %5, %6, %7").arg(
                            QString::fromStdString(streamstats.rtcp.peerSdes.cname),
                            QString::fromStdString(streamstats.rtcp.peerSdes.name),
                            QString::fromStdString(streamstats.rtcp.peerSdes.email),
                            QString::fromStdString(streamstats.rtcp.peerSdes.phone),
                            QString::fromStdString(streamstats.rtcp.peerSdes.loc),
                            QString::fromStdString(streamstats.rtcp.peerSdes.tool),
                            QString::fromStdString(streamstats.rtcp.peerSdes.note));
                callInfo["JB: FrameSize (bytes):"] = (int)streamstats.jbuf.frameSize;
                callInfo["JB: Minimum allowed prefetch (frms):"] = (int)streamstats.jbuf.minPrefetch;
                callInfo["JB: Maximum allowed prefetch (frms):"] = (int)streamstats.jbuf.maxPrefetch;
                callInfo["JB: Current burst level (frms):" ] = (int)streamstats.jbuf.burst;
                callInfo["JB: Current prefetch value (frms):"] = (int)streamstats.jbuf.prefetch;
                callInfo["JB: Current buffer size (frms):"] = (int)streamstats.jbuf.size;
                callInfo["JB: Average delay (ms):"] = (int)streamstats.jbuf.avgDelayMsec;
                callInfo["JB: Minimum delay (ms):"] = (int)streamstats.jbuf.minDelayMsec;
                callInfo["JB: Maximum delay (ms):"] = (int)streamstats.jbuf.maxDelayMsec;
                callInfo["JB: Deviation of delay (ms):"] = (int)streamstats.jbuf.devDelayMsec;
                callInfo["JB: Average burst (frms):"] = (int)streamstats.jbuf.avgBurst;
                callInfo["JB: Lost (frms):"] = (int)streamstats.jbuf.lost;
                callInfo["JB: Discarded (frms):"] = (int)streamstats.jbuf.discard;
                callInfo["JB: Number of empty on GET events:"] = (int)streamstats.jbuf.empty;
            }
        }
        catch(Error& err){
            callInfo["Error: "] = err.info().c_str();
            m_lib->m_Log->writeLog(1,(QString("Callinfo: Call info failed") + err.info().c_str()));
        }
    }
    return callInfo;
}


void Accounts::addCallToHistory(int AccID, QString callUri, int duration, s_codec codec, bool outgoing)
{
    s_account* account = getAccountByID(AccID);
    bool entryexists = false;
    for (auto histentry : account->CallHistory) {
        if(histentry.callUri == callUri){
            entryexists = true;
            histentry.codec = codec;
            histentry.count++;
            histentry.duration = duration;
            histentry.outgoing = outgoing;
            for(int i =0; i<account->CallHistory.size(); i++){      // delete the existing entry and place then modified entry as the first item
                if(account->CallHistory.at(i).callUri == callUri){
                    account->CallHistory.removeAt(i);
                    account->CallHistory.prepend(histentry);
                }
            }
            break;
        }
    }

    if (!entryexists) {
        s_callHistory newCall;
        newCall.callUri = callUri;
        newCall.codec = codec;
        newCall.duration = duration;
        newCall.outgoing = outgoing;
        newCall.codec = codec;
        newCall.count = 1;
        if (account->CallHistory.count() > 9) {
            account->CallHistory.removeLast();
        }
        account->CallHistory.prepend(newCall);
    }
    m_lib->m_Settings->saveAccConfig();
}

const QList<s_callHistory>* Accounts::getCallHistory(int AccID) {
    s_account* account = getAccountByID(AccID);
    return &account->CallHistory ;
}


void Accounts::OncallStateChanged(int accID, int role, int callId, bool remoteofferer, long calldur, int state, int lastStatusCode, QString statustxt, QString remoteUri)
{
    emit callStateChanged(accID, role, callId, remoteofferer, calldur, state, lastStatusCode, statustxt, remoteUri);
    s_account* thisAccount = getAccountByID(accID);
    s_Call* thisCall;
    for(auto& call : thisAccount->CallList){
        if(call.callId == callId){
            thisCall = &call;
            break;
        }
    }
    if(thisCall == nullptr){
        return;
    }
    thisCall->CallStatusCode = state;
    thisCall->CallStatusText = statustxt;
    thisCall->ConnectedTo = remoteUri;
    thisAccount->callStatusLastReason = statustxt;

    if(state == PJSIP_INV_STATE_EARLY){
        if(lastStatusCode == 180 && role == 1){                                                                           // autoanswer call
            acceptCall(accID,callId);
        }
        thisCall->CallStatusText = "Invite: State Early";
    }
    else if (state == PJSIP_INV_STATE_CALLING) {
        thisCall->CallStatusText = "calling";
    }
    else if(state == PJSIP_INV_STATE_CONFIRMED && lastStatusCode == 200){
        thisAccount->gpioDev->setConnected(true);
        emit m_lib->m_AudioRouter->audioRoutesChanged(m_lib->m_AudioRouter->getAudioRoutes());
    }
    else if(state == PJSIP_INV_STATE_DISCONNECTED) {
        //         if(sipStatus.fields.SipClientRegistered)
        {
            //             pjsua->sendPresenceStatus(online);
        }
    }
    m_lib->m_Log->writeLog(3,(QString("Callstate of: ") + remoteUri + " is:  " + thisCall->CallStatusText));
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
    QJsonObject info;
    for(auto& account : m_accounts){                                                   // send callInfo for every call one a second
        for(auto& call : account.CallList ){
            if(pjsua_call_is_active(call.callId) == 0){
                break;
            }
            info = getCallInfo(call.callId, account.AccID);
            emit callInfo(account.AccID, call.callId,info);
            CallInfo pjCallInfo = call.callptr->getInfo();

            if(m_MaxCallTime){                                                          // hang up calls if call time is exeeded
                QTime time = QTime::fromString(info["Call time:"].toString(), "hh:mm:ss");
                if(m_MaxCallTime <= (time.hour()*60 + time.minute())){
                    hangupCall(call.callId,account.AccID);
                    m_lib->m_Log->writeLog(3,(QString("Max call time exeeded on account ID: ")+ QString::number(account.AccID) + " call disconnected"));
                }
            }

            int emptyGetevent = info["JB: Number of empty on GET events: "].toInt();    // detect rx media loss
            if(emptyGetevent > call.lastJBemptyGETevent){  // RX media lost
                emit  callStateChanged(account.AccID, pjCallInfo.role, call.callId, pjCallInfo.remOfferer, pjCallInfo.connectDuration.sec, 7, call.CallStatusCode, QString("RX media lost since: ") + QDateTime::fromSecsSinceEpoch(call.RXlostSeconds, Qt::OffsetFromUTC).toString("hh:mm:ss"),call.ConnectedTo);
                call.lastJBemptyGETevent = emptyGetevent;
                if(call.RXlostSeconds==15){
                    qDebug() << "loss prevention";

                    //pjsua_call_reinvite(call.callId,0,nullptr);
                    pjsua_call_update(call.callId,0,nullptr);
                }
                call.RXlostSeconds++;
            }
            else if(call.RXlostSeconds){    // RX media recovered
                call.RXlostSeconds = 0;
                emit  callStateChanged(account.AccID, pjCallInfo.role, call.callId, pjCallInfo.remOfferer, pjCallInfo.connectDuration.sec, 5, pjCallInfo.state , QString::fromStdString(pjCallInfo.stateText),call.ConnectedTo);
                qDebug() << "RX Stream recovered";
            }
        }
    }
}


