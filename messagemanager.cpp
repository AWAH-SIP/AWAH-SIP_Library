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

#include "messagemanager.h"
#include "awahsiplib.h"

#define THIS_FILE		"messagemanager.cpp"

MessageManager::MessageManager(AWAHSipLib *parentLib, QObject *parent) : QObject(parent), m_lib(parentLib)
{

}

void MessageManager::sendDtmf(int callId, int AccID, QString num)
{
    s_account* account = m_lib->m_Accounts->getAccountByID(AccID);
    PJCall *m_call = Q_NULLPTR;

    for (int pos = 0; pos<account->CallList.size(); pos++){       // Check if callId is valid
            if (account->CallList.at(pos).callId == callId){
                m_call = account->CallList.at(pos).callptr;
            }
        }
    if(m_call != Q_NULLPTR){
        try{
            m_call->dialDtmf(num.toStdString());
            m_lib->m_Log->writeLog(4,QString("sendDtmf from account: " + account->name +  " DTMF digit: ") + num);
        }
        catch(Error& err){
            m_lib->m_Log->writeLog(2,QString("sendDtmf from account: " + account->name + " failed ") + err.info().c_str());
        }
    }
}



void MessageManager::slotSendMessage(int callId, int AccID, QString type, QByteArray message)
{
    s_account* account = m_lib->m_Accounts->getAccountByID(AccID);
    PJCall *m_call = Q_NULLPTR;

    for (int pos = 0; pos<account->CallList.size(); pos++){       // Check if callId is valid
        if (account->CallList.at(pos).callId == callId){
            m_call = account->CallList.at(pos).callptr;
        }
    }
    if(m_call != Q_NULLPTR){
        try{
            SendInstantMessageParam prm;
            m_lib->m_Log->writeLog(3,QString("SendMessage: Mesage type: ") + type + " Message: " + message + " sent");
            foreach(char c, message)
                prm.content+=c;
            prm.contentType = type.toStdString();
            m_call->sendInstantMessage(prm);
        }
        catch(Error& err){
            m_lib->m_Log->writeLog(2,QString("SendMessage: send message failed") + err.info().c_str());
        }
    }
}

