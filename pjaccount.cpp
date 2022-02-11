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
        acc->gpioDev->setRegistered(ai.regIsActive);
        if(ai.regIsActive && acc->CallList.isEmpty()){
            parent->sendPresenceStatus(ai.id,online);
        }
        else if(ai.regIsActive && !acc->CallList.isEmpty()){
            parent->sendPresenceStatus(ai.id,busy);
        }
        else if(!ai.regIsActive && ai.regStatus == 200){                                        // todo check me!!!!!!
            emit parent->signalSipStatus(ai.id, 0,"unregistered");
            parent->sendPresenceStatus(ai.id,unknown);
        }
    }
}

void PJAccount::onRegStarted(OnRegStartedParam &prm)
{
     AccountInfo ai = getInfo();
     m_lib->m_Log->writeLog(2, QString("RegisterStateStarted: on Account: ") + QString::fromStdString(ai.uri) + (ai.regIsConfigured? " Starting Registration" : " AccountParam is NOT Set! NOT starting Registration") + " code = " + QString::number(prm.renew));
     emit parent->regStateChanged(ai.id, ai.regIsConfigured);
     parent->sendPresenceStatus(ai.id,online);
     //parent->emit signalSipStatus(ai.id, ai.regStatus,QString::fromStdString(ai.regStatusText));
}

void PJAccount::onIncomingCall(OnIncomingCallParam &iprm)
{
    m_lib->m_Log->writeLog(3, QString("Incoming call with callId: ") + QString::number(iprm.callId));
    m_lib->m_Codecs->listCodecs();         // load Codec priority stored in config file
    AccountInfo ai = getInfo();
    parent->acceptCall(iprm.callId, ai.id);
    emit parent->signalSipStatus(ai.id, ai.regStatus,QString::fromStdString(ai.regStatusText));
}

void PJAccount::onIncomingSubscribe(OnIncomingSubscribeParam &prm){
    const pj_str_t statusCode = *pjsip_get_status_text(prm.code);
    m_lib->m_Log->writeLog(3, QString("Incoming subscribtion from URL: ") + QString::fromStdString(prm.fromUri) + " Status Code: " + QString::fromStdString(statusCode.ptr) + " Reason: " + QString::fromStdString(prm.reason));
}

int PJAccount::getAccID()
{
    AccountInfo ai = getInfo();
    return ai.id;
}
