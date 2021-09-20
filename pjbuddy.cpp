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

#include "pjbuddy.h"
#include "buddies.h"

#include <QString>
#include <QDebug>

#include "types.h"
#include "awahsiplib.h"


PJBuddy::PJBuddy(AWAHSipLib *parentLib, Buddies *parent) : m_lib(parentLib)
{
    this->parent = parent;
}

PJBuddy::~PJBuddy()
{

}

void PJBuddy::onBuddyState(){
    BuddyInfo bi;
    try {
        bi = getInfo();
    }  catch(Error& err) {
        m_lib->m_Log->writeLog(2, QString("onBuddyState:getInfo failed: ") + err.info().c_str());
        return;
    }
    m_lib->m_Log->writeLog(4, QString("onBuddyState: Buddy: ") + QString::fromStdString(bi.uri) + " is " + QString::fromStdString(bi.presStatus.statusText) + QString::fromStdString(bi.presStatus.rpidId) + QString::fromStdString(bi.presStatus.note));
    m_lib->m_Log->writeLog(4, QString("Contact: ") + QString::fromStdString(bi.contact) + " SubStateName: " + QString::fromStdString(bi.subStateName) + " SubTermReason: " + QString::fromStdString(bi.subTermReason));
    if(bi.presStatus.note == "Online"){

    }else if(bi.presStatus.note == "Busy"){
        state = 1;
    }else if(bi.presStatus.note == "NoTransit"){                            // todo remove me!
        state = 2;
    }else{
        state = 3;
    }
    m_lib->m_Buddies->changeBuddyState(QString::fromStdString(bi.uri), state);
    emit parent->BuddyStatus(QString::fromStdString(bi.uri), state);
}

void PJBuddy::onBuddyEvSubState(OnBuddyEvSubStateParam &prm){
    QString reason =  QString::fromUtf8(pjsip_event_str(prm.e.type));
    BuddyInfo bi;
    try {
        bi = getInfo();
    }  catch(Error& err) {
        m_lib->m_Log->writeLog(2, QString("onBuddyEvSubState getInfo failed: ") + err.info().c_str());
        return;
    }
    m_lib->m_Log->writeLog(4, QString("onBuddyEvSubState Event subscription state of buddy ") + QString::fromStdString(bi.uri) + " is " + reason);
}
