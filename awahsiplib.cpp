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

#include "awahsiplib.h"
#include "pjsua-lib/pjsua_internal.h"

AWAHSipLib *AWAHSipLib::AWAHSipLibInstance = NULL;
AWAHSipLib *AWAHSipLib::instance(QObject *parent)
{
    if(AWAHSipLibInstance == NULL)
        AWAHSipLibInstance = new AWAHSipLib(parent);

    return AWAHSipLibInstance;
}

AWAHSipLib::AWAHSipLib(QObject *parent) : QObject(parent)
{
    delete AWAHSipLibInstance;
    AWAHSipLibInstance = NULL;

    m_Accounts = new Accounts(this, this);
    m_AudioRouter = new AudioRouter(this, this);
    m_Buddies = new Buddies(this, this);
    m_Codecs = new Codecs(this, this);
    m_GpioDeviceManager = GpioDeviceManager::instance(this);
    m_MessageManager = new MessageManager(this, this);
    m_Settings = new Settings(this, this);

    m_GpioDeviceManager->setLib(this);

    m_pjEp = PJEndpoint::instance();
    m_pjEp->setAwahLibrary(this);

    try{
        m_pjEp->libCreate();

        // read config from config file:
        m_Settings->loadSettings();

        epCfg.uaConfig.userAgent = "AWAHSip";
        epCfg.uaConfig.natTypeInSdp = 0;

//        epCfg.medConfig.rxDropPct = RX_DROP_PACKAGE;
//        epCfg.medConfig.txDropPct = TX_DROP_PACKAGE;

        m_Log = new Log(this, this);
        m_pjEp->libInit(epCfg);
        m_pjEp->audDevManager().setNullDev();                  // set a nulldevice as masterdevice
        if(TransportProtocol=="TCP")
            m_pjEp->transportCreate(PJSIP_TRANSPORT_TCP, tCfg);
        else
            m_pjEp->transportCreate(PJSIP_TRANSPORT_UDP, tCfg);

        // Start the library (worker threads etc)
        m_pjEp->libStart();
        m_Log->writeLog(3,"**** AWAHsip lib STARTED ****");

        /* Create pool for multiple Sound Device handling */
        pool = pjsua_pool_create("awahsip", 512, 512);
        m_Settings->loadIODevConfig();
        m_Settings->loadAccConfig();
        m_Settings->loadSettings();
        m_Settings->loadAudioRoutes();
        m_Accounts->startCallInspector();
    }
    catch (Error &err){
        m_Log->writeLog(1,QString("AWAHsip: starting lib failed: ") + err.info().c_str());
    }

    m_Websocket = new Websocket(m_websocketPort, this, this);

    connect(m_Accounts, &Accounts::regStateChanged, this, &AWAHSipLib::regStateChanged);
    connect(m_Accounts, &Accounts::callStateChanged, this, &AWAHSipLib::callStateChanged);
    connect(m_MessageManager, &MessageManager::signalMessage, this, &AWAHSipLib::signalMessage);
    connect(m_Accounts, &Accounts::signalSipStatus, this, &AWAHSipLib::signalSipStatus);
    connect(m_Buddies, &Buddies::signalBuddyStatus, this, &AWAHSipLib::signalBuddyStatus);
    connect(m_Log, &Log::logMessage, this, &AWAHSipLib::logMessage);
    connect(m_AudioRouter, &AudioRouter::audioRoutesChanged, this, &AWAHSipLib::audioRoutesChanged);
    connect(m_AudioRouter, &AudioRouter::audioRoutesTableChanged, this, &AWAHSipLib::audioRoutesTableChanged);
    connect(m_Accounts, &Accounts::AccountsChanged, this, &AWAHSipLib::AccountsChanged);
    connect(m_Accounts, &Accounts::callInfo, this, &AWAHSipLib::callInfo);
    connect(m_AudioRouter, &AudioRouter::AudioDevicesChanged, this, &AWAHSipLib::AudioDevicesChanged);
    connect(m_GpioDeviceManager, &GpioDeviceManager::gpioDevicesChanged, this, &AWAHSipLib::gpioDevicesChanged);
    connect(GpioRouter::instance(), &GpioRouter::gpioRoutesChanged, this, &AWAHSipLib::gpioRoutesChanged);
    connect(GpioRouter::instance(), &GpioRouter::gpioRoutesTableChanged, this, &AWAHSipLib::gpioRoutesTableChanged);
    connect(GpioRouter::instance(), &GpioRouter::gpioStateChanged, this, &AWAHSipLib::gpioStateChanged);
    connect(m_GpioDeviceManager, &GpioDeviceManager::gpioDevicesChanged, this, &AWAHSipLib::slotIoDevicesChanged);
    connect(this, &AWAHSipLib::AudioDevicesChanged, this, &AWAHSipLib::slotIoDevicesChanged);


    connect(this, &AWAHSipLib::regStateChanged, m_Websocket, &Websocket::regStateChanged);
    connect(this, &AWAHSipLib::callStateChanged, m_Websocket, &Websocket::callStateChanged);
    //connect(this, &AWAHSipLib::signalMessage, m_Websocket, &Websocket::message);
    connect(this, &AWAHSipLib::signalSipStatus, m_Websocket, &Websocket::sipStatus);
    connect(this, &AWAHSipLib::signalBuddyStatus, m_Websocket, &Websocket::buddyStatus);
    connect(this, &AWAHSipLib::logMessage, m_Websocket, &Websocket::logMessage);
    connect(this, &AWAHSipLib::audioRoutesChanged, m_Websocket, &Websocket::audioRoutesChanged);
    connect(this, &AWAHSipLib::audioRoutesTableChanged, m_Websocket, &Websocket::audioRoutesTableChanged);
    connect(this, &AWAHSipLib::AccountsChanged, m_Websocket, &Websocket::AccountsChanged);
    connect(this, &AWAHSipLib::callInfo, m_Websocket, &Websocket::callInfo);
    connect(this, &AWAHSipLib::gpioRoutesChanged, m_Websocket, &Websocket::gpioRoutesChanged);
    connect(this, &AWAHSipLib::gpioRoutesTableChanged, m_Websocket, &Websocket::gpioRoutesTableChanged);
    connect(this, &AWAHSipLib::gpioStateChanged, m_Websocket, &Websocket::gpioStatesChanged);
    connect(this, &AWAHSipLib::IoDevicesChanged, m_Websocket, &Websocket::ioDevicesChanged);
}

AWAHSipLib::~AWAHSipLib()
{
    pjsua_call_hangup_all();
    m_Log->writeLog(3,"**** shuting down AWAHsip lib  ****");
    delete m_AudioRouter;               // remove all sound devices before destroying the endpoint.
    m_pjEp->libDestroy();
    m_Log->writeLog(3,"**** shuting down AWAHsip lib ... done ****");

    delete m_Accounts;
    delete m_Buddies;
    delete m_Codecs;
    delete m_GpioDeviceManager;
    delete m_Log;
    delete m_MessageManager;
    delete m_Settings;
}

void AWAHSipLib::prepareLib()
{
    qRegisterMetaTypeStreamOperators <QList<s_IODevices>>("QList<s_IODevices>");
    qRegisterMetaTypeStreamOperators <QList<s_account>>("QList<s_account>");
    qRegisterMetaTypeStreamOperators <QList<s_audioRoutes>>("QList<s_audioRoutes>");
    qRegisterMetaTypeStreamOperators <QList<s_callHistory>>("QList<s_callHistory>");
}

QList<s_IODevices> &AWAHSipLib::getIoDevices()
{
    m_IoDevices.clear();
    m_IoDevices.append(getAudioDevices());
    m_IoDevices.append(getGpioDevices());
    return m_IoDevices;
}

void AWAHSipLib::slotIoDevicesChanged(QList<s_IODevices> &IoDev)
{
    Q_UNUSED(IoDev)
    emit IoDevicesChanged(getIoDevices());
}

void AWAHSipLib::slotSendMessage(int callId, int AccID, QString type, QByteArray message)
{
    m_MessageManager->slotSendMessage(callId, AccID, type, message);
}

QDataStream &operator<<(QDataStream &out, const s_IODevices &obj)
{
    out << obj.devicetype << obj.inputname << obj.outputame << obj.genfrequency << obj.uid << obj.path;
    return out;
}

QDataStream &operator>>(QDataStream &in, s_IODevices &obj)
{
   in  >> obj.devicetype >> obj.inputname >> obj.outputame >> obj.genfrequency >> obj.uid >> obj.path;
   return in;
}

QDataStream &operator<<(QDataStream &out, const s_account &obj)
{
    out << obj.name << obj.user << obj.password << obj.serverURI << obj.FileRecordPath << obj.FilePlayPath << obj.uid << obj.CallHistory << obj.fixedJitterBuffer << obj.fixedJitterBufferValue << obj.autoredialLastCall;
    return out;
}

QDataStream &operator>>(QDataStream &in, s_account &obj)
{
   in  >> obj.name >> obj.user >> obj.password >> obj.serverURI >> obj.FileRecordPath >> obj.FilePlayPath >> obj.uid >> obj.CallHistory >> obj.fixedJitterBuffer >> obj.fixedJitterBufferValue >> obj.autoredialLastCall;
   return in;
}


QDataStream &operator<<(QDataStream &out, const s_audioRoutes &obj)
{
    out << obj.srcDevName << obj.destDevName << obj.level << obj.persistant;
    return out;
}

QDataStream &operator>>(QDataStream &in, s_audioRoutes &obj)
{
   in  >> obj.srcDevName >> obj.destDevName >> obj.level >> obj.persistant;
   return in;
}

QDataStream &operator<<(QDataStream &out, const s_callHistory &obj)
{
    out << obj.callUri << obj.codec << obj.duration << obj.outgoing << obj.count;
    return out;
}

QDataStream &operator>>(QDataStream &in, s_callHistory &obj)
{
   in  >> obj.callUri >> obj.codec >> obj.duration >> obj.outgoing >> obj.count;
   return in;
}

