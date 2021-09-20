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
    QString fulladdr = "sip:"+ buddyUrl +"@"+ account->serverURI;
    if(account){
        try{
            account->accountPtr->findBuddy2(fulladdr.toStdString());
        } catch(Error &err){
            if(err.status == PJ_ENOTFOUND){
                BuddyConfig cfg;
                cfg.uri = fulladdr.toStdString();
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
    }else return false;
}

bool Buddies::unregisterBuddy(int AccID, QString buddyUrl){
    s_account* account = m_lib->m_Accounts->getAccountByID(AccID);
    QString fulladdr = "sip:"+ buddyUrl +"@"+ account->serverURI;
    if(account){
        Buddy buddy;
        try{
            buddy = account->accountPtr->findBuddy2(fulladdr.toStdString());
        } catch(Error &err) {
            m_lib->m_Log->writeLog(2,QString("deleteBuddy: ") + buddyUrl + " failed: " + err.info().c_str( ));
            return false;
        }
        m_lib->m_Log->writeLog(3,QString("deleteBuddy: ") + buddyUrl + " deleted successfully");
        return true;
    }else return false;
}

void Buddies::addBuddy(QString buddyUrl, QString name, QString accUid, QJsonObject codecSettings, QString uid)
{
    if(uid.isEmpty())
        uid = createNewUID();
    s_buddy newBuddy;
    s_account * account = nullptr;
    newBuddy.Name = name;
    newBuddy.accUid = accUid;
    newBuddy.buddyUrl = buddyUrl;
    newBuddy.codec.fromJSON(codecSettings);
    newBuddy.uid = uid;
    account = m_lib->m_Accounts->getAccountByUID(accUid);
    if(account != nullptr){
        registerBuddy(account->AccID, buddyUrl);
    }
    else{
        m_lib->m_Log->writeLog(3,"AddBuddy: failed: could not find account with uid " + newBuddy.accUid);
    }
    m_buddies.append(newBuddy);
    m_lib->m_Settings->saveBuddies();
    emit BuddyEntryChanged(&m_buddies);
}

void Buddies::editBuddy(QString buddyUrl, QString name, QString accUid, QJsonObject codecSettings, QString uid)
{
    s_buddy *editBuddy = nullptr;
    editBuddy = getBuddyByUID(uid);
    if(editBuddy != nullptr){
        editBuddy->buddyUrl = buddyUrl;
        editBuddy->Name = name;
        editBuddy->accUid = accUid;
        editBuddy->codec.fromJSON(codecSettings);
        m_lib->m_Settings->saveBuddies();
        emit BuddyEntryChanged(&m_buddies);
    }
    else{
        m_lib->m_Log->writeLog(3,"editBuddy: failed: could not find buddy with uid: " + uid);
    }
}

void Buddies::removeBuddy(QString uid)
{
    QMutableListIterator<s_buddy> i(m_buddies);
    while(i.hasNext()){
        s_buddy &buddy = i.next();
        if(buddy.uid == uid){
            s_account* account = m_lib->m_Accounts->getAccountByUID(buddy.accUid);
            unregisterBuddy(account->AccID,buddy.buddyUrl);
            i.remove();
            emit BuddyEntryChanged(&m_buddies);
            m_lib->m_Settings->saveBuddies();
            break;
        }
    }
}

s_buddy* Buddies::getBuddyByUID(QString uid){
    for(auto& buddy : m_buddies){
        if(buddy.uid == uid)
            return &buddy;
    }
    return nullptr;
}

void Buddies::changeBuddyState(QString buddyUrl, int state){
    for(auto& buddy : m_buddies){
        if(buddy.buddyUrl == buddyUrl)
            buddy.status = state;
    }
}

