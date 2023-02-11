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

#ifndef BUDDIES_H
#define BUDDIES_H

#include <QObject>
#include "pjbuddy.h"
#include "types.h"
#include <QTimer>

class AWAHSipLib;

class Buddies : public QObject
{
    Q_OBJECT
public:
    explicit Buddies(AWAHSipLib *parentLib, QObject *parent = nullptr);

    /**
    * @brief this function is only used internally to register a buddy in pjsua
    */
    bool registerBuddy(s_buddy& buddy);

    /**
    * @brief this function is only used internally to remove a buddy from pjsua
    */
    bool unregisterBuddy(s_buddy& buddy);

    /**
    * @brief add a Buddy entry (a buddy can be seen as a phonebook entry)
    * @param buddyUrl the SIP server URL of the buddy
    * @param name the displayed name of the buddy
    * @param accUid the UID of the account that the buddy is attached to
    * @param codecSettings when a call is established to the buddy this codec and the settings is used
    * @param uid the uid of the buddy
    */
    void addBuddy(QString buddyUrl, QString name, QString accUid, QJsonObject codecSettings, QString uid = "");

    /**
    * @brief edit a Buddy entry
    * @param buddyUrl the SIP server URL of the buddy
    * @param name the displayed name of the buddy
    * @param accUid the UID of the account that the buddy is attached to
    * @param codecSettings when a call is established to the buddy this codec and the settings is used
    * @param uid the uid of the buddy
    */
    void editBuddy(QString buddyUrl, QString name, QString accUid, QJsonObject codecSettings, QString uid);

    /**
    * @brief remove a Buddy
    * @param uid the UID of the boddy to be removed
    */
    void removeBuddy(QString uid);

    /**
    * @brief get the configured Buddies
    * @return QList with all the buddies
    */
    QList <s_buddy>* getBuddies(){return &m_buddies; };

    /**
    * @brief get a buddy by its uid
    * @return s_buddy the buddy
    */
    s_buddy* getBuddyByUID(QString uid);


    /**
    * @brief  update a status of a buddy
    * @param  BuddyUrl the full url of the buddy
    * @param  State the new State
    */
    void changeBuddyState(QString buddyUrl, int state);

    /**
    * @brief  set the time in seconds how often the buddy state is cleared and refreshed. A low value is useful for a fast autoconnect but generates a lot of sip messages.
    * @param  sec the refresh time in seconds
    */
    void SetMaxPresenceRefreshTime(const int RefTimeSec) {maxPresenceRefreshTime = RefTimeSec; };

signals:

    /**
    * @brief signalBuddyStatus is emmited when a buddy changes his status (e.g. from online to busy etc.)
    * @param buddyUrl the SIP url of the buddy
    * @param status the status of the buddy
    */
    void BuddyStatus(QString buddyUrl, int status);

    /**
    * @brief signalBuddyEntryChanged is emmited when a buddy is added, edited or removed from the buddies list
    * @param the updated buddy list with all buddies
    */
    void BuddyEntryChanged(QList<s_buddy>* buddies);

private:
    AWAHSipLib* m_lib;
    QList <s_buddy> m_buddies;
    QTimer *m_BuddyChecker;
    uint8_t maxPresenceRefreshTime = 30;

private slots:

    /**
    * @brief the buddychecker slot is called every 10 seconds and checks if the last seen timestamp is within the time window defined by the maxPresenceRefreshTime
    * @brief if the timestamp is to old, the buddy is marked as unknown und its presence is subscribed again
    */
    void BuddyChecker();

};

#endif // BUDDIES_H
