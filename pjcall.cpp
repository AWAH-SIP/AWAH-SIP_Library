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

#include "pjcall.h"
#include <QDebug>
#include "pjmedia.h"
#include "pjsua-lib/pjsua_internal.h"
#include "awahsiplib.h"
#include <QDateTime>

#include "types.h"

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
        PJSUA2_CHECK_EXPR (pjsua_conf_connect(call->splitterSlot, pjsua_recorder_get_conf_port(call->rec_id)) );
    }
}

// Notification when call's state has changed.
void PJCall::onCallState(OnCallStateParam &prm)
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
        m_lib->m_Log->writeLog(1, QString("onCallState: Call %1 not found in CallList of Account %2:%3: Creating a new entry")
                               .arg(QString::fromStdString(ci.remoteUri), QString::number(callAcc->AccID), callAcc->name));
        s_Call newCall(callAcc->splitterSlot);                              // callist entry is created here
        newCall.callptr = this;
        newCall.callId = getId();
        newCall.CallStatusCode =  getInfo().state;
        newCall.CallStatusText = QString::fromStdString(getInfo().stateText);
        callAcc->CallList.append(newCall);
        Callopts = &newCall;
        emit m_lib->AccountsChanged(m_lib->m_Accounts->getAccounts());
    }

    parent->OncallStateChanged(ci.accId, ci.role, ci.id, ci.remOfferer, ci.connectDuration.sec,ci.state, ci.lastStatusCode, QString::fromStdString(ci.lastReason),QString::fromStdString(ci.remoteUri));

    if(ci.state == PJSIP_INV_STATE_DISCONNECTED)
    {
        //parent->setConnectDuration(ci.connectDuration.sec);
        m_lib->m_Log->writeLog(3,QString("onCallState: deleting call with id: %1 from %2 of Account %3").arg(QString::number(ci.id), QString::fromStdString(ci.remoteUri), callAcc->name));

        if(hasMedia())                                          //to be determined if it's a bug... Nope Adi encounters it... but it crashes...
        {                                                     // change this!!!! it never has media (on Andys macbook)!!!!
            m_lib->m_Log->writeLog(3,QString("onCallState: Disconnect: call has media!!!!! look at pjcall.cpp @ Line 93"));
            AudioMedia audioMedia = getAudioMedia(-1);
            int callId = ci.media.at(0).audioConfSlot;
            try {
                //first stop the mic stream, then the playback stream
                PJSUA2_CHECK_EXPR( pjsua_conf_disconnect(callAcc->splitterSlot, callId) );
                PJSUA2_CHECK_EXPR( pjsua_conf_disconnect(callId, callAcc->splitterSlot) );
                if (Callopts->player_id!= PJSUA_INVALID_ID)
                {
                    pjsua_conf_disconnect(pjsua_player_get_conf_port(Callopts->player_id), callId);
                    PJSUA2_CHECK_EXPR( pjsua_player_destroy(Callopts->player_id) );
                    Callopts->player_id = PJSUA_INVALID_ID;
                }
                if (Callopts->rec_id != PJSUA_INVALID_ID)
                {
                    pjsua_conf_disconnect(callId, pjsua_recorder_get_conf_port(Callopts->rec_id));
                    pjsua_conf_disconnect(callAcc->splitterSlot, pjsua_recorder_get_conf_port(Callopts->rec_id));
                    PJSUA2_CHECK_EXPR( pjsua_recorder_destroy(Callopts->rec_id) );
                    Callopts->rec_id = PJSUA_INVALID_ID;
                }
            }  catch (Error &err) {
                m_lib->m_Log->writeLog(1,QString("onCallState: Disconnect Error: ") + QString().fromStdString(err.info(true)));
            }
        }

        m_lib->m_Accounts->addCallToHistory(callAcc->AccID,QString::fromStdString(ci.remoteUri),ci.connectDuration.sec,Callopts->codec,!ci.remOfferer);
        QMutableListIterator<s_Call> i(callAcc->CallList);
        while(i.hasNext()){
            s_Call &callentry = i.next();                    //check if its a valid callid and remove it from the list
            if(callentry.callId == ci.id){
                i.remove();
                break;
            }
        }

        if(callAcc->CallList.count()==0){
            callAcc->gpioDev->setConnected(false);
        }
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

    AudioMedia audioMedia;
    try {
        // Get the first audio media and connect it to its Splitter
        audioMedia = getAudioMedia(-1);
        int callId = audioMedia.getPortId();
        StreamInfo streaminfo;

        // get the codec parameters, to have it ready for the callhistory
        streaminfo = getStreamInfo(0);
        s_codec callCodec;                                                                     // todo parse all parameters like bitrate etc;
        //callCodec.channelCount = streaminfo.audCodecParam.info.channelCnt;                   // pares the SDP !!!!!!!!
        //callCodec.clockRate = streaminfo.codecClockRate;
        callCodec.encodingName = QString::fromStdString(streaminfo.codecName);
        for(auto& codec : m_lib->m_Codecs->listCodecs()){                                   // search the codeclist got get displaynames and parameters
            if(codec.encodingName == QString::fromStdString(streaminfo.codecName)){         // this is wrong it gets the parameters of the local codec settings!
                callCodec.displayName = codec.displayName;
                callCodec.codecParameters = codec.codecParameters;
            }
        }
        Callopts->codec = callCodec;

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
                    PJSUA2_CHECK_EXPR(pjsua_conf_connect(slot, callId) );
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
                m_lib->m_Log->writeLog(3,QString("onCallMediaState: creating call recorder for callId %1").arg(Callopts->callId));
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
                pj_cstr(&rec_file,filename.toStdString().c_str());

                // Create recorder for call
                status = pjsua_recorder_create(&rec_file, 0, NULL, 0, 0, &Callopts->rec_id);
                if (status != PJ_SUCCESS){
                    char buf[50];
                    pj_strerror	(status,buf,sizeof (buf) );
                    m_lib->m_Log->writeLog(1,QString("onCallMediaState: Error creating call recorder: ") + buf);
                    return;
                }
                // connect active call to call recorder immediatley if there is no fileplayer configured
                else if(Callopts->player_id == PJSUA_INVALID_ID){
                    PJSUA2_CHECK_EXPR( pjsua_conf_connect(callId, pjsua_recorder_get_conf_port(Callopts->rec_id)) );
                    PJSUA2_CHECK_EXPR( pjsua_conf_connect(callAcc->splitterSlot, pjsua_recorder_get_conf_port(Callopts->rec_id)) );
                }
            } else {
                m_lib->m_Log->writeLog(2,QString("onCallMediaState: call recorder for callId %1 already exists").arg(Callopts->callId));
            }

        }
        PJSUA2_CHECK_EXPR( pjsua_conf_connect(callId, callAcc->splitterSlot) );
        PJSUA2_CHECK_EXPR( pjsua_conf_connect((callAcc->splitterSlot),callId) );

    } catch(Error& err) {
        m_lib->m_Log->writeLog(1,QString("onCallMediaState: media error ") +  err.info().c_str());
        return;
    }
}

void PJCall::onStreamCreated(OnStreamCreatedParam &prm)
{
    CallInfo ci = getInfo();
    s_account* callAcc = parent->getAccountByID(ci.accId);
    if(callAcc->fixedJitterBuffer){
        pjmedia_stream_jbuf_set_fixed((pjmedia_stream *) prm.stream, callAcc->fixedJitterBufferValue);
    }
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

/*
void PJCall::onCallRxOffer(OnCallRxOfferParam &prm)
{
    qDebug() << "onCallRxOffer statusCode" << prm.statusCode;
}
*/

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
    callAcc->gpioDev->setFromDTMF(dtmfdigit);
    m_lib->m_Log->writeLog(3,QString("Account: ") + callAcc->name + " recieved DTMF digit: " + QString().fromStdString(prm.digit));
}
