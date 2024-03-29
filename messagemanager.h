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

#ifndef MESSAGEMANAGER_H
#define MESSAGEMANAGER_H

#include <QObject>

class AWAHSipLib;

class MessageManager : public QObject
{
    Q_OBJECT
public:
    explicit MessageManager(AWAHSipLib *parentLib, QObject *parent = nullptr);

    void sendDtmf(int callId, int AccID, QString num);

public slots:
    void slotSendMessage(int callId, int AccID, QString type, QByteArray message);

signals:
    void signalMessage(QString type, QByteArray message);

private:
    AWAHSipLib* m_lib;

};

#endif // MESSAGEMANAGER_H
