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

#include "pjcall.h"
#include <QDebug>
#include "pjmedia.h"
#include "pjsua-lib/pjsua_internal.h"
#include "awahsiplib.h"
#include <QDateTime>
#include "pjmedia/sdp.h"

#include "types.h"

#define THIS_FILE		"PJCall.cpp"

using namespace pj;

void PJCall::on_media_finished(pjmedia_port *media_port, void *user_data)
{
    Q_UNUSED(media_port);
    s_Call *call = static_cast<s_Call*>(user_data);
    Call *ownObj = lookup(call->callId);
    CallInfo ci = ownObj->getInfo();
    s_account* callAcc = AWAHSipLib::instance()->m_Accounts->getAccountByID(ci.accId);

    if(ownObj == nullptr) {
        AWAHSipLib::instance()->m_Log->writeLog(1, (QString("PJCall::on_media_finished(): Got Invalid CallID: %1 PJ::Call Object lookup not succesfull!").arg(call->callId)));
        return;
    }

    if(call->callId < 0 && call->callId > (int)AWAHSipLib::instance()->epCfg.uaConfig.maxCalls) {
        AWAHSipLib::instance()->m_Log->writeLog(1, (QString("PJCall::on_media_finished(): Got Invalid CallID: %1").arg(call->callId)));
        return;
    }

    try {
        PJSUA2_CHECK_EXPR( pjsua_conf_disconnect(pjsua_player_get_conf_port(call->player_id),pjsua_call_get_conf_port(call->callId)) );
    }  catch (Error &err) {
        AWAHSipLib::instance()->m_Log->writeLog(1, (QString("PJCall::on_media_finished(): disconnect call from player failed ") + err.info().c_str()));
    }

    if(call->rec_id != INVALID_ID  ){
        PJSUA2_CHECK_EXPR (pjsua_conf_connect(pjsua_call_get_conf_port(call->callId), pjsua_recorder_get_conf_port(call->rec_id)) );
        AWAHSipLib::instance()->m_Log->writeLog(3, (QString("PJCall::on_media_finished(): Announcement for CallID: %1 finished connecting call to recorder").arg(call->callId)));
        if(!callAcc->FileRecordRXonly){
            PJSUA2_CHECK_EXPR (pjsua_conf_connect(call->splitterSlot, pjsua_recorder_get_conf_port(call->rec_id)) );
        }
    }
}

// Notification when call's state has changed.
void PJCall::onCallState(OnCallStateParam &prm)
{
    Q_UNUSED(prm);
    CallInfo ci = getInfo();
    s_account* callAcc = parent->getAccountByID(ci.accId);
    s_Call*  CalllistEntry = nullptr;
    int callid = getId();
    for(auto& call : callAcc->CallList){
        if(call.callId == callid){
            CalllistEntry = &call;
            break;
        }
    }
    if(CalllistEntry == nullptr) {
        m_lib->m_Log->writeLog(1, QString("onCallState: Call %1 not found in CallList of Account %2: %3: Creating a new entry")
                               .arg(QString::fromStdString(ci.remoteUri), QString::number(callAcc->AccID), callAcc->name));
        s_Call newCall(callAcc->splitterSlot);                              // callist entry is created here if not already done in onSDP callback
        newCall.callptr = this;
        newCall.callId = getId();
        newCall.codec = callAcc->SelectedCodec;
        newCall.CallStatusCode =  getInfo().state;
        newCall.CallStatusText = QString::fromStdString(getInfo().stateText);
        callAcc->CallList.append(newCall);
        CalllistEntry = &newCall;
        emit m_lib->AccountsChanged(m_lib->m_Accounts->getAccounts());
    }

    parent->OncallStateChanged(ci.accId, ci.role, ci.id, ci.remOfferer, ci.connectDuration.sec,ci.state, ci.lastStatusCode, QString::fromStdString(ci.lastReason),QString::fromStdString(ci.remoteUri));

    if(ci.state == PJSIP_INV_STATE_DISCONNECTED)
    {  
        if(CalllistEntry->callConfPort != -1) {
            try {
                //first stop the mic stream, then the playback stream
                pjsua_conf_disconnect(callAcc->splitterSlot, CalllistEntry->callConfPort);
                pjsua_conf_disconnect(CalllistEntry->callConfPort, callAcc->splitterSlot);
                if (CalllistEntry->player_id!= PJSUA_INVALID_ID)
                {
                    pjsua_conf_disconnect(pjsua_player_get_conf_port(CalllistEntry->player_id), CalllistEntry->callConfPort);
                    PJSUA2_CHECK_EXPR( pjsua_player_destroy(CalllistEntry->player_id) );
                    CalllistEntry->player_id = PJSUA_INVALID_ID;
                }
                if (CalllistEntry->rec_id != PJSUA_INVALID_ID)
                {
                    pjsua_conf_disconnect(CalllistEntry->callConfPort, pjsua_recorder_get_conf_port(CalllistEntry->rec_id));
                    if(!callAcc->FileRecordRXonly){
                        pjsua_conf_disconnect(callAcc->splitterSlot, pjsua_recorder_get_conf_port(CalllistEntry->rec_id));
                    }
                    PJSUA2_CHECK_EXPR( pjsua_recorder_destroy(CalllistEntry->rec_id) );
                    CalllistEntry->rec_id = PJSUA_INVALID_ID;
                    m_lib->m_Log->writeLog(3,QString("onCallState: closing recorder for call with id: %1 from %2 of Account %3").arg(QString::number(ci.id), QString::fromStdString(ci.remoteUri), callAcc->name));
                }
            }  catch (Error &err) {
                m_lib->m_Log->writeLog(1,QString("onCallState: Disconnect Error: ") + QString().fromStdString(err.info(true)));
            }
        }

        m_lib->m_Accounts->addCallToHistory(callAcc->AccID,QString::fromStdString(ci.remoteUri),ci.connectDuration.sec,CalllistEntry->codec,!ci.remOfferer);

        callAcc = parent->getAccountByID(ci.accId);         // as callHistory is stored in QList of callAccount, most likly this Pointer changed.
        QMutableListIterator<s_Call> i(callAcc->CallList);
        while(i.hasNext()){
            s_Call &callentry = i.next();
            if(callentry.callId == ci.id){                  //check if its a valid callid and remove it from the list
                i.remove();
                break;
            }
        }

        if(callAcc->CallList.count()==0){
            if(callAcc->gpioDev != nullptr){
                callAcc->gpioDev->setConnected(false);
            }
        }
        m_lib->m_Log->writeLog(3,QString("onCallState: deleting call with id: %1 from %2 of Account %3").arg(QString::number(ci.id), QString::fromStdString(ci.remoteUri), callAcc->name));
        emit m_lib->m_Accounts->AccountsChanged(m_lib->m_Accounts->getAccounts());
        //delete this;
    }
}

// Notification when call's media state has changed.
void PJCall::onCallMediaState(OnCallMediaStateParam &prm)
{
    Q_UNUSED(prm);
    CallInfo ci = getInfo();
    s_account* callAcc = parent->getAccountByID(ci.accId);
    s_Call*  Callopts = nullptr;
    for(auto& call : callAcc->CallList){
        if(call.callId == getId()){
            Callopts = &call;
            break;
        }
    }
    if(Callopts == nullptr) {
        m_lib->m_Log->writeLog(1, QString("onCallMediaState: Call %1 not found in CallList of Account %2:%3")
                               .arg(QString::fromStdString(ci.remoteUri), QString::number(callAcc->AccID), callAcc->name));
        return;
    }

    m_lib->m_Log->writeLog(3,QString("onCallMediaState: Call %1:%2 of Account %3:%4 has media: %5")
                           .arg(QString::number(Callopts->callId), QString::fromStdString(ci.remoteUri), QString::number(callAcc->AccID), callAcc->name, hasMedia() ? "true" : "false" ));

    if(!hasMedia()) return;

    AudioMedia audioMedia;
    try {
        // Get the first audio media and connect it to its Splitter
        audioMedia = getAudioMedia(-1);
        Callopts->callConfPort = audioMedia.getPortId();

        if(!callAcc->FilePlayPath.isEmpty() && ci.remOfferer){          // if a announcement is configured and call is incoming create a player
            if(Callopts->player_id == PJSUA_INVALID_ID) {
                pjmedia_port *player_media_port = nullptr;
                pj_str_t name;
                pj_status_t status = PJ_ENOTFOUND;

                // create player for playback media
                m_lib->m_Log->writeLog(3,QString("onCallMediaState: creating announcement player for callId %1").arg(Callopts->callId));
                status = pjsua_player_create(pj_cstr(&name,callAcc->FilePlayPath.toStdString().c_str()), PJMEDIA_FILE_NO_LOOP, &Callopts->player_id);
                if (status != PJ_SUCCESS) {
                    char buf[50];
                    pj_strerror	(status,buf,sizeof (buf) );
                    m_lib->m_Log->writeLog(1,QString("onCallMediaState: Error creating announcement player: ") + buf);
                } else {
                    pjsua_data* intData = pjsua_get_var();
                    const pjsua_conf_port_id slot = pjsua_player_get_conf_port(Callopts->player_id);
                    int level = -3;
                    PJSUA2_CHECK_EXPR(pjmedia_conf_adjust_rx_level(intData->mconf, slot, dBtoAdjLevel(level)));
                    PJSUA2_CHECK_EXPR(pjsua_conf_connect(slot, Callopts->callConfPort) );
                    status = pjsua_player_get_port(Callopts->player_id, &player_media_port);
                    if (status != PJ_SUCCESS){
                        char buf[50];
                        pj_strerror	(status,buf,sizeof (buf) );
                        m_lib->m_Log->writeLog(1,QString("onCallMediaState: Error getting announcement player port: ") + buf);
                        return;
                    }
                    // register media finished callback
                    status = pjmedia_wav_player_set_eof_cb2(player_media_port, Callopts, &on_media_finished);
                    if (status != PJ_SUCCESS){
                        char buf[50];
                        pj_strerror	(status,buf,sizeof (buf) );
                        m_lib->m_Log->writeLog(1,QString("onCallMediaState: Error adding sound-playback callback ") + buf);
                        return;
                    }
                }
            } else {
                m_lib->m_Log->writeLog(2,QString("onCallMediaState: announcement player for callId %1 already exists!").arg(Callopts->callId));
            }
        }

        if(!callAcc->FileRecordPath.isEmpty()){            // if a filerecorder is configured create a recorder
            if(Callopts->rec_id == PJSUA_INVALID_ID) {
                m_lib->m_Log->writeLog(3,QString("onCallMediaState: creating a call recorder for callId %1").arg(Callopts->callId));
                pj_status_t status = PJ_ENOTFOUND;
                pj_str_t rec_file;
                QDateTime local(QDateTime::currentDateTime());
                QString Year = local.toString("yyyy");
                QString Month = local.toString("MM");
                QString Day = local.toString("dd");
                QString Hour = local.toString("hh");
                QString Minute = local.toString("mm");
                QString Second = local.toString("ss");
                QString Caller = QString::fromStdString(ci.remoteUri);
                Caller.truncate(Caller.lastIndexOf("@"));
                Caller = Caller.mid(Caller.indexOf(":")+1);     // only the Name of the caller
                QString Account = callAcc->name;

                QString filename = callAcc->FileRecordPath + ".wav";
                filename.replace("%Y", Year);
                filename.replace("%M", Month);
                filename.replace("%D", Day);
                filename.replace("%h", Hour);
                filename.replace("%m", Minute);
                filename.replace("%s", Second);
                filename.replace("%C", Caller);
                filename.replace("%A",Account);

                // Create recorder for call
                status = pjsua_recorder_create(pj_cstr(&rec_file, filename.toStdString().c_str()), 0, NULL, 0, 0, &Callopts->rec_id);
                if (status != PJ_SUCCESS){
                    char buf[50];
                    pj_strerror	(status,buf,sizeof (buf) );
                    m_lib->m_Log->writeLog(1,QString("onCallMediaState: Error creating call recorder: ") + buf);
                    return;
                }
                // connect active call to call recorder immediatley if there is no fileplayer configured
                else if(Callopts->player_id == PJSUA_INVALID_ID){
                    PJSUA2_CHECK_EXPR( pjsua_conf_connect(Callopts->callConfPort, pjsua_recorder_get_conf_port(Callopts->rec_id)) );
                    if(!callAcc->FileRecordRXonly){
                        PJSUA2_CHECK_EXPR( pjsua_conf_connect(callAcc->splitterSlot, pjsua_recorder_get_conf_port(Callopts->rec_id)) );          // record audio from the far end and also the local audio (usually questions from the host)
                    }
                }
            } else {
                m_lib->m_Log->writeLog(2,QString("onCallMediaState: call recorder for callId %1 already exists").arg(Callopts->callId));
            }

        }
        PJSUA2_CHECK_EXPR( pjsua_conf_connect(Callopts->callConfPort, callAcc->splitterSlot) );
        PJSUA2_CHECK_EXPR( pjsua_conf_connect((callAcc->splitterSlot),Callopts->callConfPort) );

    } catch(Error& err) {
        m_lib->m_Log->writeLog(1,QString("onCallMediaState: media error ") +  err.info().c_str());
        return;
    }
}


void PJCall::onStreamCreated(OnStreamCreatedParam &prm)
{
    CallInfo ci = getInfo();
    pjmedia_stream_info info;
    pjmedia_stream_get_info((pjmedia_stream *) prm.stream, &info);
    s_codec remoteCodec;                                                                                    // parse codec parameters for the callhistory entry
    QString encodingName = pj2Str(info.fmt.encoding_name);
    if( encodingName == "opus"){                                                                            // convert the encoding name to a nice userfriendly name
        remoteCodec.encodingName = "opus/48000/2";
        remoteCodec.displayName = "Opus";
        remoteCodec.codecParameters = m_lib->m_Codecs->getCodecParam(remoteCodec.encodingName);             // load the default parameters to have a full set of objects
        remoteCodec.codecParameters["Bit rate mode"].toObject()["value"] = 0;
        remoteCodec.codecParameters["Channelcount"].toObject()["value"] = (int)info.fmt.channel_cnt;
        remoteCodec.codecParameters["Bit rate"].toObject()["value"] = (int)info.param->info.avg_bps;
        remoteCodec.codecParameters["Clockrate"].toObject()["value"] = (int)info.param->info.clock_rate;

    }
    else if(encodingName == "PCMU"){
        remoteCodec.encodingName = "PCMU/8000/1";
        remoteCodec.codecParameters = m_lib->m_Codecs->getCodecParam(remoteCodec.encodingName);
        remoteCodec.displayName = "G711 u-Law";
    }
    else if(encodingName == "PCMA"){
        remoteCodec.encodingName = "PCMA/8000/1";
        remoteCodec.codecParameters = m_lib->m_Codecs->getCodecParam(remoteCodec.encodingName);
        remoteCodec.displayName = "G711 A-Law";
    }
    else if(encodingName == "L16"){
        remoteCodec.displayName = "Linear";
        remoteCodec.encodingName = encodingName + "/" + QString::number(info.fmt.clock_rate) + "/" + QString::number(info.fmt.channel_cnt);
        remoteCodec.codecParameters = m_lib->m_Codecs->getCodecParam(remoteCodec.encodingName);
        remoteCodec.codecParameters["Channelcount"].toObject()["value"] = (QString) info.fmt.channel_cnt;
        remoteCodec.codecParameters["Clockrate"].toObject()["value"] = (QString)info.fmt.clock_rate;
    }
    else if(encodingName == "G722"){
        remoteCodec.displayName = "G722";
        remoteCodec.encodingName = "G722/16000/1";
        remoteCodec.codecParameters = m_lib->m_Codecs->getCodecParam(remoteCodec.encodingName);
    }
    else if(encodingName =="speex"){
        remoteCodec.displayName = "Speex";
        remoteCodec.encodingName = encodingName + "/" + QString::number(info.fmt.clock_rate) + "/" + QString::number(info.fmt.channel_cnt);
        remoteCodec.codecParameters = m_lib->m_Codecs->getCodecParam(remoteCodec.encodingName);
        remoteCodec.codecParameters["Clockrate"].toObject()["value"] = (QString)info.fmt.clock_rate;
    }
    else if(encodingName.contains("AMR")){
        remoteCodec.displayName = "AMR";
        remoteCodec.encodingName = encodingName + "/" + QString::number(info.fmt.clock_rate) + "/" + QString::number(info.fmt.channel_cnt);
        remoteCodec.codecParameters = m_lib->m_Codecs->getCodecParam(remoteCodec.encodingName);
    }
    else if(encodingName =="iLBC"){
        remoteCodec.displayName = "iLBC";
        remoteCodec.encodingName = encodingName + "/" + QString::number(info.fmt.clock_rate) + "/" + QString::number(info.fmt.channel_cnt);
        remoteCodec.codecParameters = m_lib->m_Codecs->getCodecParam(remoteCodec.encodingName);
    }
    else if(encodingName =="GSM"){
        remoteCodec.displayName = "GSM";
        remoteCodec.encodingName = encodingName + "/" + QString::number(info.fmt.clock_rate) + "/" + QString::number(info.fmt.channel_cnt);
        remoteCodec.codecParameters = m_lib->m_Codecs->getCodecParam(remoteCodec.encodingName);
    }
    else{
        remoteCodec.displayName = encodingName;
        remoteCodec.encodingName = encodingName;
    }


    s_account* callAcc = parent->getAccountByID(ci.accId);
    if(callAcc->fixedJitterBuffer){
        pjmedia_stream_jbuf_set_fixed((pjmedia_stream *) prm.stream, callAcc->fixedJitterBufferValue);
    }
    for(auto& thecall : callAcc->CallList){
        if(thecall.callId == getId()){
            if(true){
                thecall.codec = remoteCodec;
            }
            break;
        }
    }
    m_lib->m_Codecs->listCodecs();                                                          // call is established, from now on accept all codecs according to the set priorities
}

void PJCall::onCallTransferRequest(OnCallTransferRequestParam &prm)
{
    m_lib->m_Log->writeLog(3,QString("onCallTransferRequest: transfering call to: ") +  prm.dstUri.c_str() + prm.statusCode);
    CallInfo ci = getInfo();
    parent->OncallStateChanged(ci.accId, ci.role, ci.id,ci.remOfferer, ci.connectDuration.sec,ci.state, ci.lastStatusCode, QString::fromStdString(ci.lastReason),QString::fromStdString(ci.remoteUri));
}


void PJCall::onCallTransferStatus(OnCallTransferStatusParam &prm)
{
    m_lib->m_Log->writeLog(3,QString("onCallTransferStatus: ") + prm.reason.c_str());
    CallInfo ci = getInfo();
    parent->OncallStateChanged(ci.accId, ci.role, ci.id,ci.remOfferer, ci.connectDuration.sec,ci.state, ci.lastStatusCode, QString::fromStdString(ci.lastReason),QString::fromStdString(ci.remoteUri));

    if(prm.statusCode == PJSIP_SC_OK)
    {
        m_lib->m_Log->writeLog(3,QString("onCallTransferStatus: deleting call with call id ") + QString::number(ci.id));
        delete this;
    }
}


void PJCall::onCallReplaceRequest(OnCallReplaceRequestParam &prm)
{
    m_lib->m_Log->writeLog(3,QString("onCallReplaceRequest: status code ") + QString::number(prm.statusCode));
}


void PJCall::onCallSdpCreated(OnCallSdpCreatedParam &prm)
{
    CallInfo ci = getInfo();
    s_account* callAcc = nullptr;
    s_codec remoteCodec;
    QString sdpString;
    callAcc = parent->getAccountByID(ci.accId);
    if(callAcc == nullptr){
        m_lib->m_Log->writeLog(2,QString("onSdpCreated: Error account not found!"));
        return;
    }

    if(ci.role == PJSIP_ROLE_UAC){                                                              // get local SDP if we established the call
        sdpString = QString::fromStdString(prm.sdp.wholeSdp);
        remoteCodec = callAcc->SelectedCodec;
    }

    if(ci.remOfferer){
        sdpString = QString::fromStdString(prm.remSdp.wholeSdp);
    }

    s_Call*  call = nullptr;
    for(auto& thecall : callAcc->CallList){
        if(thecall.callId == getId()){
            thecall.SDP = sdpString;
            break;
        }
    }
    if(call == nullptr){
        m_lib->m_Log->writeLog(1, QString("onCallSDP: Call %1 not found in CallList of Account %2:%3: Creating a new entry")
                               .arg(QString::fromStdString(ci.remoteUri), QString::number(callAcc->AccID), callAcc->name));
        s_Call newCall(callAcc->splitterSlot);                                                                                      // callist entry is created here
        newCall.callptr = this;
        newCall.callId = getId();
        newCall.CallStatusCode =  getInfo().state;
        newCall.CallStatusText = QString::fromStdString(getInfo().stateText);
        newCall.SDP = sdpString;
        callAcc->CallList.append(newCall);
    }
    emit m_lib->AccountsChanged(m_lib->m_Accounts->getAccounts());
}

void PJCall::onInstantMessage(OnInstantMessageParam &prm)
{
    QString type = QString::fromStdString(prm.contentType);
    //QString message = QString::fromStdString(prm.msgBody);
    //qDebug() << Q_FUNC_INFO << type << message;
    QByteArray message;
    foreach(char c, prm.msgBody)
        message.append(c);
    emit m_msg->signalMessage(type, message);
}

void PJCall::onDtmfDigit(OnDtmfDigitParam &prm)
{
    char dtmfdigit = prm.digit.c_str()[0];
    CallInfo ci = getInfo();
    s_account* callAcc = nullptr;
    callAcc = parent->getAccountByID(ci.accId);
    if(callAcc == nullptr){
        m_lib->m_Log->writeLog(2,QString("onDtmfDigit: Error account not found!"));
        return;
    }
    if(callAcc->gpioDev != nullptr){
        callAcc->gpioDev->setFromDTMF(dtmfdigit);
    }
    m_lib->m_Log->writeLog(4,QString("Account: ") + callAcc->name + " recieved DTMF digit: " + QString().fromStdString(prm.digit));
}
