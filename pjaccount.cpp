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

#include "pjaccount.h"

#include <QString>
#include <QDebug>

#include "awahsiplib.h"


PJAccount::PJAccount(AWAHSipLib *parentLib, Accounts *parent) : m_lib(parentLib)
{
    this->parent = parent;
}

PJAccount::~PJAccount()
{
}

void PJAccount::onRegState(OnRegStateParam &prm) {
    AccountInfo ai = getInfo();
    s_account *acc = parent->getAccountByID(ai.id);
    Q_UNUSED(prm);
    emit parent->regStateChanged(ai.id, ai.regIsActive);
    emit parent->signalSipStatus(ai.id, ai.regStatus,QString::fromStdString(ai.regStatusText));
    if(acc != nullptr){
        if(acc->gpioDev != nullptr){
            acc->gpioDev->setRegistered(ai.regIsActive);
        }
        if(ai.regIsActive && acc->CallList.isEmpty()){
            parent->sendPresenceStatus(ai.id,online);
        }
        else if(ai.regIsActive && !acc->CallList.isEmpty()){
            parent->sendPresenceStatus(ai.id,busy);
        }
    }
}

void PJAccount::onRegStarted(OnRegStartedParam &prm)
{
     AccountInfo ai = getInfo();
     m_lib->m_Log->writeLog(2, QString("RegisterStateStarted: on Account: ") + QString::fromStdString(ai.uri) + (ai.regIsConfigured? " Starting Registration" : " AccountParam is NOT Set! NOT starting Registration") + " code = " + QString::number(prm.renew));
     emit parent->regStateChanged(ai.id, ai.regIsConfigured);
     emit parent->signalSipStatus(ai.id, ai.regStatus,QString::fromStdString(ai.regStatusText));
}

void PJAccount::onIncomingCall(OnIncomingCallParam &iprm)
{
    pjsip_rdata_sdp_info *sdpinfo;
    s_codec remoteCodec;
    m_lib->m_Log->writeLog(3, QString("Incoming call with callId: ") + QString::number(iprm.callId));
    m_lib->m_Codecs->listCodecs();                                                                                                                  // load Codec priority stored in config file. Like this all configured codecs are accepted instead of sending not acceptable
    sdpinfo =  pjsip_rdata_get_sdp_info(static_cast<pjsip_rx_data*>(iprm.rdata.pjRxData));
    for (unsigned i = 0; i < sdpinfo->sdp->media_count; i++)                                                                                        // parse the incoming sdp and set our defaults accourding to it(needed for opus config).
        if (pj_stricmp2(&sdpinfo->sdp->media[i]->desc.media, "audio") == 0) {                                                                       // If we don't do this the library offers the opus configuration stored as default.
            pjmedia_sdp_media *r_media = sdpinfo->sdp->media[i];                                                                                    // this leads to unsymetrical connetctions (e.g. opus sterero 128k and opus mono 64k in the other direction)
            pjmedia_sdp_attr *codec = pjmedia_sdp_attr_find2(r_media->attr_count, r_media->attr, "rtpmap", NULL);        // find codec type         // this attributes getupdatet in the callback onMediaCreated to chatch upddates during the negotiation phase
            if (codec != NULL) {
                QString tmp = pj2Str(codec->value);
                QStringList codectype = tmp.split(" ");
                codectype = codectype.at(1).split(";");
                remoteCodec.encodingName = codectype.first();
                if(remoteCodec.encodingName.startsWith("opus",Qt::CaseInsensitive)){                                    // convert the encoding name to a nice userfriendly name
                    remoteCodec.encodingName = "opus/48000/2";
                    remoteCodec.displayName = "Opus";
                    }
                }
                else if(remoteCodec.encodingName.startsWith("PCMU",Qt::CaseInsensitive)){
                    remoteCodec.encodingName = "PCMU/8000/1";
                    remoteCodec.displayName = "G711 u-Law";
                }
                else if(remoteCodec.encodingName.startsWith("PCMA",Qt::CaseInsensitive)){
                    remoteCodec.encodingName = "PCMA/8000/1";
                    remoteCodec.displayName = "G711 A-Law";
                }
                else if(remoteCodec.encodingName.startsWith("L16",Qt::CaseInsensitive)){
                    remoteCodec.displayName = "Linear";
                    QStringList tmp = remoteCodec.encodingName.split("/");
                    QJsonObject jsob = remoteCodec.codecParameters["Clockrate"].toObject();
                    jsob["value"] = tmp.at(1).toInt();
                    remoteCodec.codecParameters["Clockrate"] = jsob;
                    if(tmp.size() > 2){
                        QJsonObject jsob = remoteCodec.codecParameters["Channelcount"].toObject();
                        jsob["value"] = tmp.at(2).toInt();
                        remoteCodec.codecParameters["Channelcount"] = jsob;
                    }
                }
                else if(remoteCodec.encodingName.startsWith("G722",Qt::CaseInsensitive)){
                    remoteCodec.displayName = "G722";
                }
                else if(remoteCodec.encodingName.startsWith("speex",Qt::CaseInsensitive)){
                    remoteCodec.displayName = "Speex";
                }
                else if(remoteCodec.encodingName.startsWith("AMR",Qt::CaseInsensitive)){
                    remoteCodec.displayName = "AMR";
                }
                else if(remoteCodec.encodingName.startsWith("iLBC",Qt::CaseInsensitive)){
                    remoteCodec.displayName = "iLBC";
                }
                else if(remoteCodec.encodingName.startsWith("GSM",Qt::CaseInsensitive)){
                    remoteCodec.displayName = "GSM";
                }
                else
                    remoteCodec.displayName = remoteCodec.encodingName;

            pjmedia_sdp_attr *attribute = pjmedia_sdp_attr_find2(r_media->attr_count, r_media->attr, "fmtp", NULL);       // find and parse fmtp attributes                 // todo rewrite the whole section or even better: find a good solution!!!!!
            if (attribute != NULL) {                                                                                                                                        // this reads only the first paremeter depending on sdp structure we miss a lot!!!!!
                QString tmp = pj2Str(attribute->value);
                QStringList attributes = tmp.split(" ");
                attributes = attributes.at(1).split(";");
                for( int i = 0; i  < attributes.size() ; i++){
//                        if(attributes.at(i).contains("0-1")){              // could be 0-15 or 0-16 depending on the client                                               // started to detect if the other side supports DTMF and only send tones if true
//                            qDebug() << "yes we can send DTMF";                                                                                                           // todo find a better place for this
//                        }
                    if(attributes.at(i).contains("maxaveragebitrate")){
                        QStringList value = attributes.at(i).split("=");
                        QJsonObject jsob = remoteCodec.codecParameters["Bit rate"].toObject();
                        jsob["value"] = value.at(1).toDouble();
                        remoteCodec.codecParameters["Bit rate"] = jsob;
                    }
                    else if(attributes.at(i).contains("stereo")){
                        QStringList value = attributes.at(i).split("=");
                        QJsonObject jsob = remoteCodec.codecParameters["Channelcount"].toObject();
                        jsob["value"] = value.at(1).toInt() + 1;        // +1 if stereo=0 -> channelcount is 1
                        remoteCodec.codecParameters["Channelcount"] = jsob;
                    }
                    else if(attributes.at(i).contains("cbr")){
                        QStringList value = attributes.at(i).split("=");
                        QJsonObject jsob = remoteCodec.codecParameters["Bit rate mode"].toObject();
                        jsob["value"] = value.at(1).toInt();
                        remoteCodec.codecParameters["Bit rate mode"] = jsob;
                    }
                    else if(attributes.at(i).contains("useinbandfec")){
                        QStringList value = attributes.at(i).split("=");
                        QJsonObject jsob = remoteCodec.codecParameters["Inband FEC"].toObject();
                        jsob["value"] = value.at(1).toInt();
                        remoteCodec.codecParameters["Inband FEC"] = jsob;
                    }
                }
            }
        }               
    m_lib->m_Codecs->setCodecParam(remoteCodec);  
    AccountInfo ai = getInfo();
    parent->acceptCall(iprm.callId, ai.id);
    emit parent->signalSipStatus(ai.id, ai.regStatus,QString::fromStdString(ai.regStatusText));
}

void PJAccount::onIncomingSubscribe(OnIncomingSubscribeParam &prm){
    const pj_str_t statusCode = *pjsip_get_status_text(prm.code);
    m_lib->m_Log->writeLog(3, QString("Incoming subscribtion from URL: ") + QString::fromStdString(prm.fromUri) + " Status Code: " + QString::fromStdString(statusCode.ptr) + " Reason: " + QString::fromStdString(prm.reason));
    AccountInfo ai = getInfo();
    s_account *acc = parent->getAccountByID(ai.id);
    QString URI = QString::fromStdString(prm.fromUri);
    if(acc){
        Buddy buddyptr;
        int startPos = URI.indexOf('<')+1;
        int endPos = URI.indexOf('>');
        int length = endPos - startPos;
        QString URL = (URI.mid(startPos, length));
        m_lib->m_Buddies->changeBuddyState(URL, online);
    }
}


int PJAccount::getAccID()
{
    AccountInfo ai = getInfo();
    return ai.id;
}
