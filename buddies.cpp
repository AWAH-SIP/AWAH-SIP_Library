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

#include "buddies.h"
#include "awahsiplib.h"

#define THIS_FILE		"buddies.cpp"

Buddies::Buddies(AWAHSipLib *parentLib, QObject *parent) : QObject(parent), m_lib(parentLib)
{
    m_BuddyChecker = new QTimer(this);
    m_BuddyChecker->setInterval(10000);
    connect(m_BuddyChecker, SIGNAL(timeout()), this, SLOT(BuddyChecker()));
    m_BuddyChecker->start();
}

bool Buddies::registerBuddy(int AccID, QString buddyUrl){
    s_account* account = m_lib->m_Accounts->getAccountByID(AccID);
    QString fulladdr = "sip:"+ buddyUrl +"@"+ account->serverURI;
    Buddy buddyptr;
    if(account){
        buddyptr = account->accountPtr->findBuddy2(fulladdr.toStdString());
        if(!buddyptr.isValid()){
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
        else{
            m_lib->m_Log->writeLog(3,"RegisterBuddy: Buddy register failed: Buddy already exists!");
            return false;
        }
    }
    else return false;
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
            s_account* account = nullptr;
            account = m_lib->m_Accounts->getAccountByUID(buddy.accUid);
            if(account != nullptr){
                unregisterBuddy(account->AccID,buddy.buddyUrl);
            }
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
    emit BuddyStatus(buddyUrl, state);
    int startPos = buddyUrl.indexOf("sip:") + 4;
    int endPos = buddyUrl.indexOf('@');
    int length = endPos - startPos;
    QString URL = (buddyUrl.mid(startPos, length));
    for(auto& buddy : m_buddies){
        if(buddy.buddyUrl == URL){
            buddy.status = state;
            if(buddy.status == busy){
                buddy.lastSeen = QDateTime::currentDateTime();
                buddy.autoconnectInProgress = false;
            }
            if(buddy.status  == unknown){
                buddy.autoconnectInProgress = false;
            }
            else if(buddy.status  == online){
                buddy.lastSeen = QDateTime::currentDateTime();
                QList<s_account>* accounts = m_lib->m_Accounts->getAccounts();
                for(auto& account : *accounts){                                 // if buddy changed to online search accounts if it is marked as autoconnect
                    if (buddy.uid == account.autoconnectToBuddyUID){
                        if(account.SIPStatusCode == 200 && account.CallList.length() == 0 && buddy.autoconnectInProgress == false && account.autoconnectEnable){                     // only connect if account is registered, no call is active and no autoconnect is aready in progress
                            m_lib->m_Log->writeLog(3,"Autoconnect to buddy: connecting from account: " + account.name + " to: " + buddy.Name+ " " + buddy.buddyUrl);
                            buddy.autoconnectInProgress = true;
                            m_lib->m_Accounts->makeCall(buddy.buddyUrl,account.AccID,buddy.codec);
                            break;
                        }
                    }
                }
            }
        }
    }
}

void Buddies::BuddyChecker()
{
    QDateTime now = QDateTime::currentDateTime();
    for(auto& buddy : m_buddies){
        if(buddy.lastSeen.isValid() && (uint8_t)buddy.lastSeen.secsTo(now) > buddy.maxPresenceRefreshTime){
            s_account* account = m_lib->m_Accounts->getAccountByUID(buddy.accUid);
            Buddy buddyptr;
            if(account){
                QString fulladdr = "sip:"+ buddy.buddyUrl +"@"+ account->serverURI;
                buddyptr = account->accountPtr->findBuddy2(fulladdr.toStdString());
                if(buddyptr.isValid()){
                    changeBuddyState( fulladdr, 0);                                     // to detect offline buddys reliably we set the buddy as offline after maxPresenceRefreshTime
                    buddyptr.subscribePresence(false);                                  // then we subscribe the buddy again like this we have only the online buddys in the list
                    buddyptr.subscribePresence(true);
                }
            }
        }
    }
}
