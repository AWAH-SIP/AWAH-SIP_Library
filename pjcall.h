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

#ifndef PJCall_H
#define PJCall_H

#include <QObject>
#include <pjsua2.hpp>

using namespace pj;

class Accounts;
class AWAHSipLib;
class MessageManager;

class PJCall : public Call
{
public:
    PJCall(Accounts *parent, AWAHSipLib *parentLib, MessageManager *parentMsg, Account &acc, int call_id = PJSUA_INVALID_ID)
    : Call(acc, call_id)
    {
        this->parent = parent;
        this->m_lib = parentLib;
        this->m_msg = parentMsg;
        this->hold = false;
#ifdef Q_OS_ANDROID
        this->audioMedia = Q_NULLPTR;
        this->captureMedia = Q_NULLPTR;
#else
        this->audioMedia = 0;
        this->captureMedia = 0;
#endif
    }

    ~PJCall()
    {
        this->audioMedia = Q_NULLPTR;
        this->captureMedia = Q_NULLPTR;
        parent = Q_NULLPTR;
    }

    // Notification when call's state has changed.
    virtual void onCallState(OnCallStateParam &prm);

    virtual void onCallMediaState(OnCallMediaStateParam &prm);

    virtual void onCallTransferRequest(OnCallTransferRequestParam &prm);

    virtual void onCallTransferStatus(OnCallTransferStatusParam &prm);

    virtual void onCallReplaceRequest(OnCallReplaceRequestParam &prm);

    virtual void onStreamCreated(OnStreamCreatedParam &prm);

    // virtual void onCallRxOffer(OnCallRxOfferParam &prm);

    virtual void onInstantMessage(OnInstantMessageParam &prm);

    virtual void onDtmfDigit(OnDtmfDigitParam &prm);

    inline bool isOnHold()
    {
        return hold;
    }

    inline void setHoldTo(bool hold)
    {
        this->hold = hold;
    }

private:
    static void on_media_finished(pjmedia_port *media_port, void *user_data);

    Accounts *parent;
    AWAHSipLib* m_lib;
    MessageManager *m_msg;
    AudioMedia *audioMedia;
    AudioMedia *captureMedia;
    bool hold;
};

#endif // PJCall_H
