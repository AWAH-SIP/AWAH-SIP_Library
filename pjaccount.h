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

#ifndef PJAccount_H
#define PJAccount_H

#include <pjsua2.hpp>

#include <QString>
#include <QObject>

using namespace pj;

class AWAHSipLib;
class Accounts;

// Subclass to extend the Account and get notifications etc.
class PJAccount : public Account
{

public:

    PJAccount(AWAHSipLib *parentLib, Accounts *parent);
    ~PJAccount();

    virtual void onRegState(OnRegStateParam &prm);

    virtual void onRegStarted(OnRegStartedParam &prm);

    virtual void onIncomingCall(OnIncomingCallParam &iprm);

    virtual void onIncomingSubscribe(OnIncomingSubscribeParam &prm);

    virtual int getAccID();

private:
    Accounts *parent;
    AWAHSipLib* m_lib;

};

#endif // PJAccount_H
