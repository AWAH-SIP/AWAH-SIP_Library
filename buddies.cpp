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
}

bool Buddies::registerBuddy(s_buddy& buddy){
    s_account* account = m_lib->m_Accounts->getAccountByUID(buddy.accUid);
    QString fulladdr = "sip:"+ buddy.buddyUrl +"@"+ account->serverURI;

    if(account == nullptr){
        return false;
    }
    if(account->SIPStatusCode != 200){          // only register buddys when account is registered, otherwise the subsrcibe messages can cause problems because they are not trusted messages on certain sbc's
        return false;
    }
    BuddyConfig cfg;
    cfg.uri = fulladdr.toStdString();
    cfg.subscribe = true;
    buddy.buddyptr = new PJBuddy();
    try {
        buddy.buddyptr->create(*account->accountPtr, cfg);
        buddy.buddyptr->subscribePresence(true);
        m_lib->m_Log->writeLog(3,QString("RegisterBuddy: Buddy registered: ") + buddy.Name + " -> " + buddy.buddyUrl);

    } catch(Error& err) {
        m_lib->m_Log->writeLog(1,QString("RegisterBuddy: Buddy register failed: ") + err.info().c_str());
        return false;
    }
    return true;
}

bool Buddies::unregisterBuddy(s_buddy& buddy){
    s_account* account = m_lib->m_Accounts->getAccountByUID(buddy.accUid);
    if(account == nullptr){
        return false;
    }
        QString fulladdr = "sip:"+ buddy.buddyUrl +"@"+ account->serverURI;
        changeBuddyState(fulladdr, unknown);
        if(buddy.buddyptr == nullptr){
            return false;
        }
        try{
            buddy.buddyptr->subscribePresence(0);
             delete buddy.buddyptr;
             buddy.buddyptr = nullptr;
        } catch(Error &err) {
            m_lib->m_Log->writeLog(2,QString("unregisterBuddy: ") + buddy.buddyUrl + " failed: " + err.info().c_str( ));
            return false;
      }
        m_lib->m_Log->writeLog(3,QString("unregisterBuddy: ") + buddy.Name + " " + buddy.buddyUrl + " unregistered successfully");
        return true; 
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
        registerBuddy(newBuddy);
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
        unregisterBuddy(*editBuddy);
        editBuddy->buddyUrl = buddyUrl;
        editBuddy->Name = name;
        editBuddy->accUid = accUid;
        editBuddy->codec.fromJSON(codecSettings);
        registerBuddy(*editBuddy);
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
                unregisterBuddy(buddy);
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

void Buddies::StartBuddyChecker()
{
    pj_timer_entry_init(&m_timerEntry, 0, NULL, &BuddyChecker);
    pj_time_val timeDelay;
    timeDelay.sec=10;
    timeDelay.msec=12;
    pjsip_endpt_schedule_timer(pjsua_get_pjsip_endpt(), &m_timerEntry,&timeDelay);
}

void Buddies::BuddyChecker(pj_timer_heap_t *timer_heap, pj_timer_entry *entry)
{
    PJ_UNUSED_ARG(timer_heap);
    QDateTime now = QDateTime::currentDateTime();
    QList<s_buddy> *buddies = AWAHSipLib::instance()->m_Buddies->getBuddies();
    for(auto& buddy : *buddies){
        if(buddy.lastSeen.isValid() && (uint8_t)buddy.lastSeen.secsTo(now) > AWAHSipLib::instance()->m_Buddies->maxPresenceRefreshTime){
            s_account* account = AWAHSipLib::instance()->m_Accounts->getAccountByUID(buddy.accUid);
            if(account){
                QString fulladdr = "sip:"+ buddy.buddyUrl +"@"+ account->serverURI;
                AWAHSipLib::instance()->m_Buddies->changeBuddyState( fulladdr, unknown);                                          // set the buddy as offline after maxPresenceRefreshTime to detect offline buddies reliably
                if(account->SIPStatusCode == 200 && buddy.buddyptr != nullptr){
                    buddy.buddyptr->subscribePresence(false);                                  // then subscribe the buddy again to enshure that only the online buddies are in the buddy list
                    buddy.buddyptr->subscribePresence(true);
                }
            }
        }
    }
    pj_time_val loctimeDelay;                                                       // restart Timer
    loctimeDelay.msec=12;
    loctimeDelay.sec=10;
    pjsip_endpt_schedule_timer(pjsua_get_pjsip_endpt(), entry, &loctimeDelay);
}
