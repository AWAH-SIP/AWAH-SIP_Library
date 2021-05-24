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

#ifndef BUDDIES_H
#define BUDDIES_H

#include <QObject>
#include "pjbuddy.h"
#include "types.h"

class AWAHSipLib;

class Buddies : public QObject
{
    Q_OBJECT
public:
    explicit Buddies(AWAHSipLib *parentLib, QObject *parent = nullptr);

    bool registerBuddy(int AccID, QString buddyUrl);
    bool deleteBuddy(int AccID, QString buddyUrl);
    void addBuddy(QString buddyUrl, QString name);
    void removeBuddy(QString buddyUrl);
    const QList <s_buddy> getBuddies(){return m_buddies; };

signals:
    void signalBuddyStatus(QString buddy, int status);


private:
    AWAHSipLib* m_lib;
    QList <s_buddy> m_buddies;

};

#endif // BUDDIES_H
