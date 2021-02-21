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

#include "buddies.h"
#include "awahsiplib.h"

#define THIS_FILE		"buddies.cpp"

Buddies::Buddies(AWAHSipLib *parentLib, QObject *parent) : QObject(parent), m_lib(parentLib)
{

}

bool Buddies::registerBuddy(int AccID, QString buddyUrl){
    s_account* account = m_lib->m_Accounts->getAccountByID(AccID);
    if(account){
        try{
            account->accountPtr->findBuddy2(buddyUrl.toStdString());
        } catch(Error& err){
            if(err.status == PJ_ENOTFOUND){
                BuddyConfig cfg;
                cfg.uri = buddyUrl.toStdString();
                cfg.subscribe = true;
                PJBuddy *buddy = new PJBuddy(m_lib, this);
                try {
                    buddy->create(*account->accountPtr, cfg);
                } catch(Error& err) {
                    m_lib->m_Log->writeLog(1,QString("RegisterBuddy: Buddy register failed: ") + err.info().c_str());
                    return false;
                }
                return true;
            }
        }
        m_lib->m_Log->writeLog(3,"RegisterBuddy: Buddy register failed: Buddy already exists!");
        return false;
    }else   return false;
}

bool Buddies::deleteBuddy(int AccID, QString buddyUrl){
    s_account* account = m_lib->m_Accounts->getAccountByID(AccID);
    if(account){
        Buddy buddy;
        try{
            buddy = account->accountPtr->findBuddy2(buddyUrl.toStdString());
        } catch(Error& err) {
            m_lib->m_Log->writeLog(2,QString("deleterBuddy: ") + buddyUrl + " failed: " + err.info().c_str( ));
            return false;
        }
        return true;
    }else   return false;
}

