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

#include "../include/pjcall.h"
#include <QDebug>
#include "pjsua-lib/pjsua_internal.h"
#include "../include/awahsiplib.h"
#include <QDateTime>
#include "pj/string.h"
#include <pj/pool.h>

#include "../include/types.h"

#define THIS_FILE		"PJCall.cpp"

using namespace pj;

void PJCall::on_media_finished(pjmedia_port *media_port, void *user_data)
{
    Q_UNUSED(media_port);
    s_Call *call = static_cast<s_Call*>(user_data);
    Call *ownObj = lookup(call->callId);
    if(ownObj == nullptr) {
        AWAHSipLib::instance()->m_Log->writeLog(1, (QString("PJCall::on_media_finished(): Got Invalid CallID: %1 PJ::Call Object lookup not succesfull!").arg(call->callId)));
        return;
    }

    CallInfo ci = ownObj->getInfo();
    s_account* callAcc = AWAHSipLib::instance()->m_Accounts->getAccountByID(ci.accId);

    if(call->callId < 0 && call->callId > (int)AWAHSipLib::instance()->epCfg.uaConfig.maxCalls) {
        AWAHSipLib::instance()->m_Log->writeLog(1, (QString("PJCall::on_media_finished(): Got Invalid CallID: %1").arg(call->callId)));
        return;
    }

    try {
        const pjsua_conf_port_id pslot = pjsua_player_get_conf_port(call->player_id);
        for (auto slot : call->perCallConfSlots) {
            PJSUA2_CHECK_EXPR( pjsua_conf_disconnect(pslot, slot) );
        }
    }  catch (Error &err) {
        AWAHSipLib::instance()->m_Log->writeLog(1, (QString("PJCall::on_media_finished(): disconnect call from player failed ") + err.info().c_str()));
    }

    if(call->rec_id != INVALID_ID  ){
        // After announcement finishes, route call audio to recorder (left channel if stereo)
        if (!call->perCallConfSlots.isEmpty()) {
            PJSUA2_CHECK_EXPR (pjsua_conf_connect(call->perCallConfSlots.first(), pjsua_recorder_get_conf_port(call->rec_id)) );
        }
        AWAHSipLib::instance()->m_Log->writeLog(3, (QString("PJCall::on_media_finished(): Announcement for CallID: %1 finished connecting call to recorder").arg(call->callId)));
        // No account splitter path anymore TODO: reimplemet if(!callAcc->FileRecordRXonly)
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
        s_Call newCall;                              // callist entry is created here if not already done in onSDP callback
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
        m_lib->m_AudioRouter->scheduleConferenceRefresh(150);
        emit m_lib->m_Accounts->AccountsChanged(m_lib->m_Accounts->getAccounts());
        delete this;
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
        // Get the first audio media (auto-added by pjsua)
        audioMedia = getAudioMedia(-1);

        // Remove the auto-added conf port (always; mono and stereo)
        if (audioMedia.getPortId() != -1) {
            pjsua_conf_remove_port(audioMedia.getPortId());
        }
        // Ensure PJSUA won't try to auto-remove a conf port we already removed
        {
            pjsua_data* intData = pjsua_get_var();
            if (intData) {
                pjsua_call *pcall = &intData->calls[getId()];
                if (pcall && pcall->audio_idx >= 0) {
                    pjsua_call_media *call_med = &pcall->media[pcall->audio_idx];
                    call_med->strm.a.conf_slot = PJSUA_INVALID_ID;
                }
            }
        }

        // Determine channels via stored codec info
        const int chcnt = Callopts->codec.codecParameters["Channelcount"].toObject()["value"].toInt(1);
        pjsua_conf_port_info masterInfo;
        pjsua_conf_get_port_info(0, &masterInfo);

        if (chcnt <= 1) {
            // Mono: re-add stream port with SIP naming and wire player/recorder below
            pjmedia_port *streamPort = Callopts->callStreamPort;
            if (streamPort) {
                QString remoteUri = QString::fromStdString(ci.remoteUri);
                QString remoteNum = remoteUri; int c=remoteNum.indexOf(":"); int a=remoteNum.indexOf("@");
                if (c>=0 && a>c) remoteNum = remoteNum.mid(c+1, a-c-1);
                QString name = QString("SIP:%1-%2-Ch:1").arg(callAcc->name).arg(remoteNum);
                pj_strdup2(m_lib->pool, &streamPort->info.name, name.toStdString().c_str());
                pjsua_conf_port_id slot;
                if (pjsua_conf_add_port(m_lib->pool, streamPort, &slot)==PJ_SUCCESS) {
                    Callopts->perCallConfSlots.clear();
                    Callopts->perCallConfSlots.append(slot);
                }
            }
        } else {
            // Stereo+: create splitcomb and one master_port, register both rev channels
            if (!m_callPool) {
                QString poolName = QString("SIP-CALL-%1").arg(getId());
                m_callPool = pj_pool_create(&pjsua_get_var()->cp.factory, poolName.toStdString().c_str(), 4096, 4096, NULL);
            }
            pjmedia_port *streamPort = Callopts->callStreamPort;
            if (streamPort) {
                const pjmedia_port_info &pi = streamPort->info;
                int srate = PJMEDIA_PIA_SRATE(&pi);
                int spf = PJMEDIA_PIA_SPF(&pi);
                if (srate <= 0) srate = (int)callAcc->SelectedCodec.codecParameters["Clockrate"].toObject()["value"].toInt(48000);
                if (spf <= 0) spf = (srate * 20 * 2)/1000;
                pj_status_t st = pjmedia_splitcomb_create(m_callPool ? m_callPool : m_lib->pool, srate, 2, spf, 16, 0, &Callopts->perCallSplitComb);
                if (st == PJ_SUCCESS && Callopts->perCallSplitComb) {
                    st = pjmedia_master_port_create(m_callPool ? m_callPool : m_lib->pool, streamPort, Callopts->perCallSplitComb, 0, &Callopts->mp_stream_to_split);
                    if (st == PJ_SUCCESS) {
                        pjmedia_master_port_start(Callopts->mp_stream_to_split);
                        pjsua_conf_port_info masterInfo; pjsua_conf_get_port_info(0, &masterInfo);
                        for (int ch=0; ch<2; ++ch) {
                            pjmedia_port *revch=nullptr;
                            st = pjmedia_splitcomb_create_rev_channel(m_callPool ? m_callPool : m_lib->pool, Callopts->perCallSplitComb, ch, 32, &revch);
                            if (st != PJ_SUCCESS || !revch) continue;
                            QString remoteUri = QString::fromStdString(ci.remoteUri);
                            QString remoteNum = remoteUri; int c=remoteNum.indexOf(":"); int a=remoteNum.indexOf("@");
                            if (c>=0 && a>c) remoteNum = remoteNum.mid(c+1, a-c-1);
                            QString name = QString("SIP:%1-%2-Ch:%3").arg(callAcc->name).arg(remoteNum).arg(ch+1);
                            pj_strdup2(m_callPool ? m_callPool : m_lib->pool, &revch->info.name, name.toStdString().c_str());
                            pjsua_conf_port_id mslot=PJSUA_INVALID_ID;
                            if (pjsua_conf_add_port(m_callPool ? m_callPool : m_lib->pool, revch, &mslot)==PJ_SUCCESS) {
                                Callopts->perCallMonoPorts.append(revch);
                                Callopts->perCallConfSlots.append(mslot);
                                pjsua_conf_connect(masterInfo.slot_id, mslot);
                                pjsua_data* intData = pjsua_get_var();
                                pjmedia_conf_adjust_conn_level(intData->mconf, masterInfo.slot_id, mslot, -128);
                            }
                        }
                    }
                }
            }
        }

        // After final ports exist, create and wire file player and recorder
        {
            const bool isStereo = (chcnt > 1);
            // Announcement player (incoming only)
            if (!callAcc->FilePlayPath.isEmpty() && ci.remOfferer && Callopts->player_id == PJSUA_INVALID_ID) {
                pj_str_t name; pj_status_t status;
                status = pjsua_player_create(pj_cstr(&name, callAcc->FilePlayPath.toStdString().c_str()), PJMEDIA_FILE_NO_LOOP, &Callopts->player_id);
                if (status == PJ_SUCCESS) {
                    pjsua_data* intData = pjsua_get_var();
                    const pjsua_conf_port_id pslot = pjsua_player_get_conf_port(Callopts->player_id);
                    int level = -3;
                    pjmedia_conf_adjust_rx_level(intData->mconf, pslot, dBtoAdjLevel(level));
                    if (!Callopts->perCallConfSlots.isEmpty()) {
                        for (auto slot : Callopts->perCallConfSlots) pjsua_conf_connect(pslot, slot);
                    }
                    pjmedia_port *player_media_port = nullptr;
                    if (pjsua_player_get_port(Callopts->player_id, &player_media_port) == PJ_SUCCESS) {
                        pjmedia_wav_player_set_eof_cb2(player_media_port, Callopts, &on_media_finished);
                    }
                } else {
                    char buf[64]; pj_strerror(status, buf, sizeof(buf));
                    m_lib->m_Log->writeLog(1, QString("onCallMediaState: Error creating announcement player: ") + buf);
                }
            }

            // Recorder
            if (!callAcc->FileRecordPath.isEmpty() && Callopts->rec_id == PJSUA_INVALID_ID) {
                pj_status_t status; pj_str_t rec_file;
                QDateTime local(QDateTime::currentDateTime());
                QString Year = local.toString("yyyy");
                QString Month = local.toString("MM");
                QString Day = local.toString("dd");
                QString Hour = local.toString("hh");
                QString Minute = local.toString("mm");
                QString Second = local.toString("ss");
                QString Caller = QString::fromStdString(ci.remoteUri);
                Caller.truncate(Caller.lastIndexOf("@"));
                Caller = Caller.mid(Caller.indexOf(":")+1);
                QString filename = callAcc->FileRecordPath + ".wav";
                filename.replace("%Y", Year).replace("%M", Month).replace("%D", Day).replace("%h", Hour)
                        .replace("%m", Minute).replace("%s", Second).replace("%C", Caller).replace("%A", callAcc->name);
                status = pjsua_recorder_create(pj_cstr(&rec_file, filename.toStdString().c_str()), 0, NULL, 0, 0, &Callopts->rec_id);
                if (status == PJ_SUCCESS) {
                    const pjsua_conf_port_id rslot = pjsua_recorder_get_conf_port(Callopts->rec_id);
                    // Only connect immediately if no announcement player is active
                    if (Callopts->player_id == PJSUA_INVALID_ID) {
                        if (!Callopts->perCallConfSlots.isEmpty()) {
                            pjsua_conf_connect(Callopts->perCallConfSlots[0], rslot);
                        }
                    }
                } else {
                    char buf[64]; pj_strerror(status, buf, sizeof(buf));
                    m_lib->m_Log->writeLog(1, QString("onCallMediaState: Error creating call recorder: ") + buf);
                }
            }

        }

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
        remoteCodec.codecParameters["Channelcount"].toObject()["value"] = QString::number(info.fmt.channel_cnt);
        remoteCodec.codecParameters["Clockrate"].toObject()["value"] = QString::number(info.fmt.clock_rate);
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
        remoteCodec.codecParameters["Clockrate"].toObject()["value"] = QString::number(info.fmt.clock_rate);
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
    // Ensure pjsua does not destroy the media port it registers; we'll manage our allocations
    prm.destroyPort = false;
    m_lib->m_Log->writeLog(3, QString("onStreamCreated: set destroyPort=false for callId %1").arg(getId()));

    /* Per-call splitcomb/mapping moved to onCallMediaState. Keep destroyPort=false here
       and only cache the stream media port as needed. */
    if (info.fmt.channel_cnt >= 1) {
        pjmedia_port* streamPort = nullptr;
        if (pjmedia_stream_get_port((pjmedia_stream*)prm.stream, &streamPort) == PJ_SUCCESS && streamPort) {
            s_account* acc = parent->getAccountByID(ci.accId);
            for (auto &c : acc->CallList) {
                if (c.callId == getId()) { c.callStreamPort = streamPort; break; }
            }
        }
    }
    m_lib->m_Log->writeLog(3,QString("onStreamCreated: cached stream port for callId %1 (splitcomb setup in onCallMediaState)").arg(getId()));
    m_lib->m_Codecs->listCodecs();                                                          // call is established, from now on accept all codecs according to the set priorities
}

void PJCall::onCallTransferRequest(OnCallTransferRequestParam &prm)
{
    m_lib->m_Log->writeLog(3,QString("onCallTransferRequest: transfering call to: ") +  prm.dstUri.c_str() + QString::number(prm.statusCode));
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
        s_Call newCall;                                                                                      // callist entry is created here
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
    for (char c : prm.msgBody)
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

void PJCall::onStreamDestroyed(OnStreamDestroyedParam &prm)
{
    Q_UNUSED(prm);
    CallInfo ci = getInfo();
    s_account* callAcc = parent->getAccountByID(ci.accId);
    if (!callAcc) return;

    s_Call* callEntry = nullptr;
    for (auto &c : callAcc->CallList) {
        if (c.callId == getId()) { callEntry = &c; break; }
    }
    if (!callEntry) return;

    m_lib->m_Log->writeLog(3, QString("onStreamDestroyed: cleaning media for callId %1").arg(getId()));

    if (callEntry->rec_id != PJSUA_INVALID_ID) {
        const pjsua_conf_port_id rslot = pjsua_recorder_get_conf_port(callEntry->rec_id);
        m_lib->m_AudioRouter->removeAllRoutesFromSlot(rslot);
        pjsua_recorder_destroy(callEntry->rec_id);
        callEntry->rec_id = PJSUA_INVALID_ID;
    }

    if (callEntry->player_id != PJSUA_INVALID_ID) {
        const pjsua_conf_port_id pslot = pjsua_player_get_conf_port(callEntry->player_id);
        m_lib->m_AudioRouter->removeAllRoutesFromSlot(pslot);
        pjsua_player_destroy(callEntry->player_id);
        callEntry->player_id = PJSUA_INVALID_ID;
    }

    if (!callEntry->perCallConfSlots.isEmpty()) {
        for (auto slot : callEntry->perCallConfSlots) {
            m_lib->m_AudioRouter->removeAllRoutesFromSlot(slot);
            pjsua_conf_remove_port(slot);
        }
        callEntry->perCallConfSlots.clear();
    }
    callEntry->perCallMonoPorts.clear();

    if (callEntry->mp_stream_to_split) {
        pjmedia_master_port_stop(callEntry->mp_stream_to_split);
        pjmedia_master_port_destroy(callEntry->mp_stream_to_split, PJ_FALSE);
        callEntry->mp_stream_to_split = nullptr;
    }
    if (callEntry->mp_split_to_stream) {
        pjmedia_master_port_stop(callEntry->mp_split_to_stream);
        pjmedia_master_port_destroy(callEntry->mp_split_to_stream, PJ_FALSE);
        callEntry->mp_split_to_stream = nullptr;
    }
    callEntry->perCallSplitComb = nullptr; // pool-owned

    if (m_callPool) {
        pj_pool_release(m_callPool);
        m_callPool = nullptr;
    }

    m_lib->m_AudioRouter->scheduleConferenceRefresh(150);
}
