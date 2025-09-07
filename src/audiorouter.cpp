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

#include "../include/audiorouter.h"
#include "../include/awahsiplib.h"
#include "pjsua-lib/pjsua_internal.h"
#include <pjmedia/port.h>
#include <pjmedia/splitcomb.h>
#include "pj/string.h"
#include "../include/LatencyMonitorDevice.h"
#include <QDebug>
#include <QThread>
#include <QSettings>
#include <cmath>

#define THIS_FILE		"audiorouter.cpp"


AudioRouter::AudioRouter(AWAHSipLib *parentLib, QObject *parent) : QObject(parent), m_lib(parentLib)
{ 
    m_SoundDeviceInspectorTimer = new QTimer(this);
    m_SoundDeviceInspectorTimer->setInterval(5000);
    connect(m_SoundDeviceInspectorTimer, SIGNAL(timeout()), this, SLOT(SoundDeviceInspector()));
    m_sounddevCount = pjmedia_snd_get_dev_count();
    m_SoundDeviceInspectorTimer->start();

    // Debounced conf refresh timer
    m_confRefreshDebounceTimer = new QTimer(this);
    m_confRefreshDebounceTimer->setSingleShot(true);
    connect(m_confRefreshDebounceTimer, &QTimer::timeout, this, [this]() {
        this->conferenceBridgeChanged();
    });

    // Debounced route evaluation timer
    m_routeEvalDebounceTimer = new QTimer(this);
    m_routeEvalDebounceTimer->setSingleShot(true);
    connect(m_routeEvalDebounceTimer, &QTimer::timeout, this, [this]() {
        // First reconcile actual bridge connections
        bool changed = reconcileActiveConnections();
        // Then re-apply parent routes across all current children to heal any missed links
        for (auto it = m_parentKeyToChildSlots.constBegin(); it != m_parentKeyToChildSlots.constEnd(); ++it) {
            const QString &parentKey = it.key();
            const QList<int> &children = it.value();
            const AudioEntityType ptype = Entity_Unknown; // not needed by apply
            for (int child : children) {
                applyParentRoutesToChildSlot(child, ptype, parentKey);
            }
        }
        if (changed) emitRoutesChanged();
    });
}

AudioRouter::~AudioRouter()
{
    m_SoundDeviceInspectorTimer->stop();
    // Unregister per-parent players/recorders and destroy their ports
    pjsua_data* intData = pjsua_get_var();
    if (!m_parentToPlayerSlot.isEmpty()) {
        for (auto it = m_parentToPlayerSlot.constBegin(); it != m_parentToPlayerSlot.constEnd(); ++it) {
            int pslot = it.value();
            if (pslot != PJSUA_INVALID_ID && intData && intData->mconf) {
                pjmedia_conf_disconnect_port_from_sources(intData->mconf, pslot);
                pjmedia_conf_disconnect_port_from_sinks(intData->mconf, pslot);
                pjsua_conf_remove_port(pslot);
            }
        }
        m_parentToPlayerSlot.clear();
    }
    if (!m_parentToRecorderSlot.isEmpty()) {
        for (auto it = m_parentToRecorderSlot.constBegin(); it != m_parentToRecorderSlot.constEnd(); ++it) {
            int rslot = it.value();
            if (rslot != PJSUA_INVALID_ID && intData && intData->mconf) {
                pjmedia_conf_disconnect_port_from_sources(intData->mconf, rslot);
                pjmedia_conf_disconnect_port_from_sinks(intData->mconf, rslot);
                pjsua_conf_remove_port(rslot);
            }
        }
        m_parentToRecorderSlot.clear();
    }
    QList<s_IODevices> *audioDevs = getAudioDevices();
    if(themaster != nullptr){
        pjmedia_master_port_stop(themaster);
        pjmedia_master_port_destroy(themaster,false);
    }
    for(int i = 0;i<audioDevs->count();i++) {                    // close all sound device before deinit library to prevent assertion fault!
        if(audioDevs->at(i).devicetype == SoundDevice) {
            pjmedia_snd_port_destroy(audioDevs->at(i).soundport);
            m_lib->m_Log->writeLog(3,(QString("Sound device closed: ") + audioDevs->at(i).inputname));
        }
        if(audioDevs->at(i).devicetype == FileRecorder) {
            pjsua_conf_remove_port(audioDevs->at(i).portNo.first());
            m_lib->m_Log->writeLog(3,(QString("Recorder removed: ") + audioDevs->at(i).outputame));
        }
    }
}


QStringList AudioRouter::listInputSoundDev(){
    pjmedia_aud_dev_refresh() ;
    QStringList snddevlist;
    QString devname;
    for (const AudioDevInfo &audiodev : m_lib->m_pjEp->audDevManager().enumDev2()){
        devname = QString::fromStdString(audiodev.name);
        if(audiodev.inputCount>0){
            snddevlist << devname;
        }
    }
    return snddevlist;
}

QStringList AudioRouter::listOutputSoundDev(){
    pjmedia_aud_dev_refresh() ;
    QStringList snddevlist;
    QString  devname;
    for (const AudioDevInfo &audiodev : m_lib->m_pjEp->audDevManager().enumDev2()){
        devname = QString::fromStdString(audiodev.name);
        if(audiodev.outputCount>0){
            snddevlist << devname;
        }  
    }
    return snddevlist;
}


int AudioRouter::getSoundDevID(QString DeviceName)
{
    int id = 0;
    for (const AudioDevInfo &audiodev : m_lib->m_pjEp->audDevManager().enumDev2()){
        if (audiodev.name == DeviceName.toStdString()){
            return id;
        }
        id++;
    }
    return -1;
}

void AudioRouter::AddClockingDevice(int recordDevId, int playbackDevId, QString uid){
    pjmedia_snd_port *soundport;
    pjmedia_port *masterport;
    pj_status_t status;
    AudioDevInfo recorddev, playbackdev;
    int samples_per_frame, channelCnt, slot;
    QList<int> connectedSlots;
    s_IODevices Audiodevice;
    if(recordDevId == -1){
        m_lib->m_pjEp->audDevManager().setNullDev();
        m_lib->m_Log->writeLog(3,QString("AddClockingDevice: Selected device not found, new router clocksource is set to internal"));
        return;
    }
    pjsua_data* pjsuavar = pjsua_get_var();
     masterport = pjsua_set_no_snd_dev();
     pjsua_set_ec(0,0);
     status = pjmedia_master_port_create(m_lib->pool, pjsuavar->null_port, masterport, 0, &themaster);
     if (status != PJ_SUCCESS){
             char buf[50];
             pj_strerror	(status,buf,sizeof (buf) );
             m_lib->m_Log->writeLog(1,(QString("AddClockingDevice: create master port failed: ") + buf));
             return;
         }
     status = pjmedia_master_port_start	(themaster);
     if (status != PJ_SUCCESS){
             char buf[50];
             pj_strerror	(status,buf,sizeof (buf) );
             m_lib->m_Log->writeLog(1,(QString("AddClockingDevice: start master port failed: ") + buf));
             return;
         }

    recorddev =  m_lib->m_pjEp->audDevManager().getDevInfo(recordDevId);
    playbackdev = m_lib->m_pjEp->audDevManager().getDevInfo(playbackDevId);

    if(recorddev.inputCount >= playbackdev.outputCount){                              // if rec and playback device have different channelcount
        channelCnt =recorddev.inputCount;                                             // use the bigger number
    }
    else channelCnt = playbackdev.outputCount;

    if (channelCnt > 64){                                                             // important edit splitcomb.c line 34  #define MAX_CHANNELS from 16 to 64
        channelCnt = 64;                                                              // limit the number of channels acorrding to MAX_CHANNEL set
    }
    if (channelCnt == 0){
        m_lib->m_Log->writeLog(3,"AddClockingDevice: Device has either no input or no outputs!" );
        return;
    }
    samples_per_frame = m_lib->epCfg.medConfig.clockRate * m_lib->epCfg.medConfig.audioFramePtime * channelCnt /  1000;
    status =   pjmedia_snd_port_create(
                /* pointer to the memory pool */ m_lib->pool,
                /* Id record device*/            recordDevId,
                /* Id pb device*/                playbackDevId,
                /* clock rate*/                  m_lib->epCfg.medConfig.clockRate,
                /*channel count*/                channelCnt,
                /*samples per frame(ptime)*/     samples_per_frame ,
                /* bits per sample*/             16,
                /* options*/                     0,
                /*pointer to the new sound port*/  &soundport);
    if (status != PJ_SUCCESS) {
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(1,(QString("AddClockingDevice: adding Audio device failed: ") + buf));
        return;
    }
 m_lib->m_Log->writeLog(3,(QString("AddClockingDevice:mediaconfig clockrate: ") + QString::number(m_lib->epCfg.medConfig.clockRate)));
 m_lib->m_Log->writeLog(3,(QString("AddClockingDevice:masterportinfo spf: ") + QString::number(samples_per_frame)));
    pjmedia_port *splitcomb;
    status = pjmedia_splitcomb_create(
                /* pointer to the memory pool */        m_lib->pool,
                /* clock rate*/                         m_lib->epCfg.medConfig.clockRate,
                /*channel count */                      channelCnt,
                /*samples per frame*/                   samples_per_frame,
                /* bits per sample*/                    16,
                /* options*/                            0,
                &splitcomb);
    if (status != PJ_SUCCESS){
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(1,(QString("AddClockingDevice: create splitcomb port failed: ") + buf));
        return;
    }

    for (int i = 0; i<channelCnt;i++)
    {
        pjmedia_port *revch;
        QString name = "AD:" + uid + "-Ch:" + QString::number(i+1);
        status = pjmedia_splitcomb_create_rev_channel(m_lib->pool, splitcomb, i, 0, &revch);
        if (status != PJ_SUCCESS){
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf) );
            m_lib->m_Log->writeLog(1,(QString("AddClockingDevice: could not create splittcomb revchannel: ") + buf));
            return;
        }
        pj_strdup2(m_lib->pool, &revch->info.name, name.toStdString().c_str());
        status = pjsua_conf_add_port(m_lib->pool, revch, &slot);
        if (status != PJ_SUCCESS){
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf) );
            m_lib->m_Log->writeLog(1,(QString("AddClockingDevice: adding  conference port failed: ") + buf));
            return;
        }
        // Use AudioRouter API to track non-persistent keepalives (both directions) at muted level
        connectConfPort(0, slot, -96, false);
        connectConfPort(slot, 0, -96, false);
        status = pjmedia_snd_port_connect(soundport, splitcomb);
        if (status != PJ_SUCCESS){
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf) );
            m_lib->m_Log->writeLog(1,(QString("AddClockingDevice: connecting sound port failed: ") + buf));
            return;
        }
        connectedSlots.append(slot);
    }
    Audiodevice.inputname = QString::fromStdString(recorddev.name);                      // update devicelist for saving and recalling current setup
    Audiodevice.outputame = QString::fromStdString(playbackdev.name);
    Audiodevice.devicetype = SoundDevice;
    Audiodevice.uid = uid;
    Audiodevice.PBDevID = playbackDevId;
    Audiodevice.RecDevID = recordDevId;
    Audiodevice.portNo = connectedSlots;
    Audiodevice.soundport = soundport;
    Audiodevice.inChannelCount = recorddev.inputCount;
    Audiodevice.outChannelCount = playbackdev.outputCount;
    bool devicefound = false;
    for (auto & existingaudiodev : m_AudioDevices){                                     // uptate existing audio dev when offline device is changing to online
        if (existingaudiodev.uid == uid){
            existingaudiodev = Audiodevice;
            devicefound = true;
            break;
        }
    }
    if (!devicefound){
         m_AudioDevices.append(Audiodevice);
    }
    m_lib->m_Settings->saveIODevConfig();
    scheduleConferenceRefresh(150);
    emit AudioDevicesChanged(m_AudioDevices);
    return;
}

void AudioRouter::addAudioDevice(int recordDevId, int playbackDevId, QString uid){
    pjmedia_snd_port *soundport;
    pj_status_t status;
    pjsua_conf_port_info masterPortInfo;
    AudioDevInfo recorddev, playbackdev;
    int slot;
    int samples_per_frame, channelCnt;
    QList<int> connectedSlots;
    s_IODevices Audiodevice;

    for(auto& device : m_AudioDevices){
        if(device.devicetype == SoundDevice){
            if(device.RecDevID >-1 && device.RecDevID >-1 && (device.RecDevID == recordDevId || device.PBDevID == playbackDevId)){
                m_lib->m_Log->writeLog(3,"AddAudioDevice: error device not added. Device already exists!" );
                return;
            }
        }
    }

    recorddev =  m_lib->m_pjEp->audDevManager().getDevInfo(recordDevId);
    playbackdev = m_lib->m_pjEp->audDevManager().getDevInfo(playbackDevId);
    if(uid.isEmpty())
        uid = createNewUID();

    if(recorddev.inputCount >= playbackdev.outputCount){                              // if rec and playback device have different channelcount
        channelCnt =recorddev.inputCount;                                             // use the bigger number
    }
    else channelCnt = playbackdev.outputCount;

    if (channelCnt > 64){                                                             // important edit splitcomb.c line 34  #define MAX_CHANNELS from 16 to 64
        channelCnt = 64;                                                              // limit the number of channels acorrding to MAX_CHANNEL set
    }
    if (channelCnt == 0){
        m_lib->m_Log->writeLog(3,"AddAudioDevice: Device has either no input or no outputs!" );
        return;
    }

    status = pjsua_conf_get_port_info( 0, &masterPortInfo );                           // get the clockrate from master port
    if (status != PJ_SUCCESS) {
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(1,(QString("AddAudioDevice: Error while reading master port info") + buf));
        return;
    }
    samples_per_frame = masterPortInfo.clock_rate * m_lib->epCfg.medConfig.audioFramePtime * channelCnt /  1000;

    status =   pjmedia_snd_port_create(
                /* pointer to the memory pool */ m_lib->pool,
                /* Id record device*/            recordDevId,
                /* Id pb device*/                playbackDevId,
                /* clock rate*/                  masterPortInfo.clock_rate,
                /*channel count*/                channelCnt,
                /*samples per frame(ptime)*/     samples_per_frame ,
                /* bits per sample*/             masterPortInfo.bits_per_sample,
                /* options*/                     0,
                /*pointer to the new sound port*/  &soundport);
    if (status != PJ_SUCCESS) {
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(1,(QString("AddAudioDevice: adding Audio decive failed: ") + buf));
        return;
    }
// m_lib->m_Log->writeLog(3,(QString("AddClockingDevice:masterportinfo clockrate: ") + QString::number(masterPortInfo.clock_rate)));
// m_lib->m_Log->writeLog(3,(QString("AddClockingDevice:masterportinfo spf: ") + QString::number(samples_per_frame)));
    pjmedia_port *splitcomb;
    status = pjmedia_splitcomb_create(
                /* pointer to the memory pool */        m_lib->pool,
                /* clock rate*/                         masterPortInfo.clock_rate,
                /*channel count */                      channelCnt,
                /*samples per frame*/                   samples_per_frame,
                /* bits per sample*/                    16,
                /* options*/                            0,
                &splitcomb);
    if (status != PJ_SUCCESS){
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(1,(QString("AddAudioDevice: create splitcomb port failed: ") + buf));
        return;
    }
    for (int i = 0; i<channelCnt;i++)
    {
        pjmedia_port *revch;
        QString name = "AD:" + uid + "-Ch:" + QString::number(i+1);
        status = pjmedia_splitcomb_create_rev_channel(m_lib->pool, splitcomb, i, 0, &revch);
        if (status != PJ_SUCCESS){
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf) );
            m_lib->m_Log->writeLog(1,(QString("Add audiodevice: could not create splittcomb revchannel: ") + buf));
            return;
        }
        pj_strdup2(m_lib->pool, &revch->info.name, name.toStdString().c_str());
        status = pjsua_conf_add_port(m_lib->pool, revch, &slot);
        if (status != PJ_SUCCESS){
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf) );
            m_lib->m_Log->writeLog(1,(QString("Add audiodevice: adding  conference port failed: ") + buf));
            return;
        }

        status = pjmedia_snd_port_connect(soundport, splitcomb);
        if (status != PJ_SUCCESS){
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf) );
            m_lib->m_Log->writeLog(1,(QString("Add audiodevice: connecting sound port failed: ") + buf));
            return;
        }
        connectedSlots.append(slot);
    }

    Audiodevice.inputname = QString::fromStdString(recorddev.name);                      // update devicelist for saving and recalling current setup
    Audiodevice.outputame = QString::fromStdString(playbackdev.name);
    Audiodevice.devicetype = SoundDevice;
    Audiodevice.uid = uid;
    Audiodevice.PBDevID = playbackDevId;
    Audiodevice.RecDevID = recordDevId;
    Audiodevice.portNo = connectedSlots;
    Audiodevice.soundport = soundport;
    Audiodevice.inChannelCount = recorddev.inputCount;
    Audiodevice.outChannelCount = playbackdev.outputCount;
    bool devicefound = false;
    for (auto & existingaudiodev : m_AudioDevices){                                     // uptate existing audio dev when offline device is changing to online
        if (existingaudiodev.uid == uid){
            existingaudiodev = Audiodevice;
            devicefound = true;
            break;
        }
    }
    if (!devicefound){
         m_AudioDevices.append(Audiodevice);
    }
    // Create non-persistent keepalive routes to stabilize latency (master <-> AD:Ch)
    for (int i = 0; i < Audiodevice.portNo.size(); ++i) {
        int ch = i + 1;
        int chSlot = Audiodevice.portNo.at(i);
        if (chSlot == PJSUA_INVALID_ID) continue;
        if (ch <= (int)Audiodevice.outChannelCount) {
            connectConfPort(0, chSlot, -96, false);
        }
        if (ch <= (int)Audiodevice.inChannelCount) {
            connectConfPort(chSlot, 0, -96, false);
        }
    }
    m_lib->m_Settings->saveIODevConfig();
    scheduleConferenceRefresh(150);
    emit AudioDevicesChanged(m_AudioDevices);
    return;
}

void AudioRouter::setAudioDeviceToOffline(QString inputName, QString outputName, QString uid)
{
    s_IODevices* offlineDevice = nullptr;
    pj_status_t status;
    for(auto& device : m_AudioDevices){
        if(device.uid == uid){
            offlineDevice = &device;
            break;
        }
    }
    if(offlineDevice == nullptr){                   // device is offline and never seen during runtime
        s_IODevices Audiodevice;
        Audiodevice.inputname = inputName;
        Audiodevice.outputame = outputName;
        Audiodevice.PBDevID = -1;
        Audiodevice.RecDevID = -1;
        Audiodevice.devicetype = SoundDevice;
        Audiodevice.uid = uid;
        m_AudioDevices.append(Audiodevice);
        m_lib->m_Settings->saveIODevConfig();
        // Keep existing persistent routes visible by using virtual slots through reconcile
        scheduleRouteEvaluation(50);
        emit AudioDevicesChanged(m_AudioDevices);
        return;
    }
    else{                                               // device was online and is lost now. Some cleanup is needed
        if(offlineDevice->devicetype == SoundDevice)
        {
            if(offlineDevice->PBDevID > -1 && offlineDevice->RecDevID > -1){
                for(auto& slot : offlineDevice->portNo){
                    try{
                        pj_status_t status;
                        QMutableListIterator<s_audioRoutes> i(m_audioRoutes);
                        while(i.hasNext()){
                            s_audioRoutes& route = i.next();
                            if(route.srcSlot == slot || route.destSlot == slot){
                                status = pjsua_conf_disconnect(route.srcSlot, route.destSlot);
                                if (status != PJ_SUCCESS){
                                    char buf[50];
                                    pj_strerror	(status,buf,sizeof (buf) );
                                    m_lib->m_Log->writeLog(2,(QString("setAudioDeviceToOffline: disconnect slot failed from slot: ") + QString::number(route.srcSlot) + " : " + buf));
                                }
                                // Do NOT remove persistent routes; they will be maintained as desired state and resolved via virtuals
                                if(!route.persistant){
                                    i.remove();
                                }
                            }
                        }
                        status = pjsua_conf_remove_port(slot);
                        if (status != PJ_SUCCESS){
                            char buf[50];
                            pj_strerror	(status,buf,sizeof (buf));
                            m_lib->m_Log->writeLog(1,(QString("setAudioDeviceToOffline: could not remove port - ERROR: ") + buf));
                            return;
                        }
                    }
                    catch(Error &err)
                    {
                        m_lib->m_Log->writeLog(1,(QString("setAudioDeviceToOffline: failed: - ERROR: " ) +  err.info().c_str()));
                        return;
                    }
                }
                status = pjmedia_snd_port_destroy(offlineDevice->soundport);
                if (status != PJ_SUCCESS){
                    char buf[50];
                    pj_strerror	(status,buf,sizeof (buf));
                    m_lib->m_Log->writeLog(1,(QString("setAudioDeviceToOffline: could not remove sound device - ERROR: ") + buf));
                    return;
                }
                m_lib->m_Log->writeLog(3,(QString("setAudioDeviceToOffline: removing: ")  +   offlineDevice->inputname));
            }
        }
        offlineDevice->PBDevID = -1;
        offlineDevice->RecDevID = -1;
        emit AudioDevicesChanged(m_AudioDevices);
        scheduleConferenceRefresh(150);
        // Reconcile to keep persistent routes visible via virtual slots and prune non-persistent
        scheduleRouteEvaluation(50);
        m_lib->m_Settings->saveIODevConfig();
    }
}

void AudioRouter::removeAudioDevice(QString uid)
{
    pj_status_t status;
    s_IODevices* deviceToRemove = nullptr;
    for(auto& device : m_AudioDevices){
        if(device.uid == uid){
            deviceToRemove = &device;
            break;
        }
    }
    if(deviceToRemove == nullptr){
        m_lib->m_Log->writeLog(2,"removeAudioDevice: device not found");
        return;
    }

    if(deviceToRemove->devicetype > FileRecorder){
        m_lib->m_Log->writeLog(3,"removeAudioDevice: device not an audio device: nothing removed!");
    }

    for(auto& slot : deviceToRemove->portNo){
        try{
            removeAllRoutesFromSlot(slot);
            status = pjsua_conf_remove_port(slot);
            if (status != PJ_SUCCESS){
                char buf[50];
                pj_strerror	(status,buf,sizeof (buf));
                m_lib->m_Log->writeLog(1,(QString("removeAudioDevice: could not remove port - ERROR: ") + buf));
                return;
            }
        }
        catch(Error &err)
        {
            m_lib->m_Log->writeLog(1,(QString("removeAudioDevice: failed: - ERROR: " ) +  err.info().c_str()));
            return;
        }
    }
    if(deviceToRemove->devicetype == SoundDevice)
    {
        if(deviceToRemove->PBDevID > -1 && deviceToRemove->RecDevID > -1){
            status = pjmedia_snd_port_destroy(deviceToRemove->soundport);
            if (status != PJ_SUCCESS){
                char buf[50];
                pj_strerror	(status,buf,sizeof (buf));
                m_lib->m_Log->writeLog(1,(QString("removeAudioDevice: could not remove sound device - ERROR: ") + buf));
                return;
            }
            m_lib->m_Log->writeLog(3,(QString("removeAudioDevice: removing: ")  +   deviceToRemove->inputname));
        }
    }

    if(deviceToRemove->devicetype == FilePlayer)
    {
        status = pjsua_player_destroy(deviceToRemove->PBDevID);
        if (status != PJ_SUCCESS){
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf));
            m_lib->m_Log->writeLog(1,(QString("removeAudioDevice: could not remove file player - ERROR: ") + buf));
            return;
        }
        status = pjmedia_port_destroy(deviceToRemove->mediaport);
        if (status != PJ_SUCCESS){
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf));
            m_lib->m_Log->writeLog(1,(QString("removeAudioDevice: could not remove player port - ERROR: ") + buf));
            return;
        }
        m_lib->m_Log->writeLog(3,(QString("removeAudioDevice: removing: ")  +   deviceToRemove->inputname));
    }

    if(deviceToRemove->devicetype == FileRecorder)
    {
        status = pjsua_recorder_destroy(deviceToRemove->RecDevID);
        if (status != PJ_SUCCESS){
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf));
            m_lib->m_Log->writeLog(1,(QString("removeAudioDevice: could not remove file player - ERROR: ") + buf));
            return;
        }
        status = pjmedia_port_destroy(deviceToRemove->mediaport);
        if (status != PJ_SUCCESS){
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf));
            m_lib->m_Log->writeLog(1,(QString("removeAudioDevice: could not remove recorder port - ERROR: ") + buf));
            return;
        }
        m_lib->m_Log->writeLog(3,(QString("removeAudioDevice: removing: ")  +   deviceToRemove->inputname));
    }

    if(deviceToRemove->devicetype == TestToneGenerator)
    {
       status = pjmedia_port_destroy(deviceToRemove->mediaport);
        if (status != PJ_SUCCESS){
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf));
            m_lib->m_Log->writeLog(1,(QString("removeAudioDevice: could not remove generator port - ERROR: ") + buf));
            return;
        }
        m_lib->m_Log->writeLog(3,(QString("removeAudioDevice: removing: ")  +   deviceToRemove->inputname));
    }

    QMutableListIterator<s_IODevices> i(m_AudioDevices);
    while(i.hasNext()){
        s_IODevices &device = i.next();
        if(device.uid == deviceToRemove->uid){
            i.remove();
            break;
        }
    }
    removeAllCustomNamesWithUID(uid);
    scheduleConferenceRefresh(150);
    m_lib->m_Settings->saveIODevConfig();
    emit AudioDevicesChanged(m_AudioDevices);
}

void AudioRouter::addToneGen(int freq, QString uid){
    pjsua_data* intData = pjsua_get_var();
    pj_status_t status;
    pjmedia_port *genPort;
    if(uid.isEmpty())
        uid = createNewUID();
    QString name = "AD:" + uid + "-ToneGen " + QString::number(freq) + "Hz";
    pj_str_t label;
    pj_strdup2(m_lib->pool, &label, name.toStdString().c_str());
    pjsua_conf_port_info masterPortInfo;
    int slot;
    s_IODevices Audiodevice;

    //get info about master conference port
    status = pjsua_conf_get_port_info( 0, &masterPortInfo );
    if (status != PJ_SUCCESS) {
        char buf[50];
        pj_strerror (status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(2,(QString("AddToneGen: Error while reading master port info") + buf));
        return;
    }

    status = pjmedia_tonegen_create2(m_lib->pool, &label,  masterPortInfo.clock_rate, 1, 2 * masterPortInfo.samples_per_frame, 16,PJMEDIA_TONEGEN_LOOP, &genPort);
    if (status != PJ_SUCCESS) {char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(2,(QString("AddToneGen: Unable to create tone generator: ") + buf));
        return;
    }

    status = pjsua_conf_add_port(m_lib->pool, genPort, &slot);
    if (status != PJ_SUCCESS) {
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(2,(QString("AddToneGen: connecting tone generator to confbridge failed: ") + buf));
        return;
    }
    int level = -3;
    status = pjmedia_conf_adjust_rx_level(intData->mconf, slot, dBtoAdjLevel(level));
    if (status != PJ_SUCCESS) {
        return;
    }


    pjmedia_tone_desc tones = {(short)freq,0,1000,0,0,0}; // selected frequency, no second freq, 1000ms on, 0ms off, no options

    status = pjmedia_tonegen_play(genPort, 1, &tones, 0);
    if (status != PJ_SUCCESS) {
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(2,(QString("AddToneGen: Generate tone failed: ") + buf));
    }

    QString Tempstring = "Generator ";
    Audiodevice.devicetype = TestToneGenerator;                             // update devicelist for saving and recalling current setup
    Audiodevice.uid = uid;
    Audiodevice.inputname = Tempstring.append(QString::number(freq).append("Hz"));
    Audiodevice.genfrequency = freq;
    Audiodevice.portNo.append(slot);
    Audiodevice.mediaport = genPort;
    m_AudioDevices.append(Audiodevice);
    m_lib->m_Settings->saveIODevConfig();
    scheduleConferenceRefresh(150);
    // Apply any pending routes targeting this recorder
    scheduleRouteEvaluation(50);
    emit AudioDevicesChanged(m_AudioDevices);
    return;
}


void AudioRouter::addLatencyMonitor(QString uid)
{
    if(uid.isEmpty()) uid = createNewUID();
    QString name = QString("Latency Monitor %1").arg(uid.left(8));
    LatencyMonitorDevice *dev = new LatencyMonitorDevice(m_lib);
    if (!dev->create(0, name)) { delete dev; m_lib->m_Log->writeLog(1, QString("addLatencyMonitor: create failed")); return; }

    const QString parentKey = QString("LM:%1").arg(uid);
    const QString sessionKey = parentKey;
    // Use the same splitcomb/attach path as calls/WebRTC to expose per-channel reverse ports
    attachStreamChannels(parentKey, sessionKey, QString("LatencyMon-%1").arg(uid.left(8)), QString(""), dev->port(), 2);

    s_IODevices Audiodevice;
    Audiodevice.devicetype = LatencyMonitor;
    Audiodevice.uid = uid;
    Audiodevice.inputname = name;
    Audiodevice.mediaport = dev->port();
    // Capture the created reverse-channel conf slots for bookkeeping
    QList<int> revSlots = m_sessionToConfSlots.value(sessionKey);
    for (int s : revSlots) Audiodevice.portNo.append(s);
    m_AudioDevices.append(Audiodevice);
    m_latencyMonitors[uid] = dev;
    scheduleConferenceRefresh(150);
    m_lib->m_Settings->saveIODevConfig();
    scheduleRouteEvaluation(50);
    emit AudioDevicesChanged(m_AudioDevices);
}

void AudioRouter::removeLatencyMonitor(QString uid)
{
    for (int i=0;i<m_AudioDevices.size();++i) {
        if (m_AudioDevices[i].uid == uid && m_AudioDevices[i].devicetype == LatencyMonitor) {
            // Tear down splitcomb/master and reverse slots associated with this LM
            const QString sessionKey = QString("LM:%1").arg(uid);
            detachParentStream(sessionKey);
            // Destroy the LatencyMonitor media
            LatencyMonitorDevice *dev = m_latencyMonitors.take(uid);
            if (dev) { dev->destroy(); delete dev; }
            m_AudioDevices.removeAt(i);
            scheduleConferenceRefresh(150);
            emit AudioDevicesChanged(m_AudioDevices);
            return;
        }
    }
}

void AudioRouter::resetLatencyMonitorStats(QString uid)
{
    LatencyMonitorDevice *dev = m_latencyMonitors.value(uid, nullptr);
    if (dev) dev->resetStats();
}

QJsonObject AudioRouter::getLatencyMonitorStats(QString uid)
{
    LatencyMonitorDevice *dev = m_latencyMonitors.value(uid, nullptr);
    if (!dev) return {};
    auto summary = dev->getSummaryStats();
    auto measurements = dev->getMeasurements();
    QJsonArray arr; for (const auto &m : measurements) {
        arr.append(QJsonObject{{"t", (double)m.tsMs}, {"c1", m.rttCh1Ms}, {"c2", m.rttCh2Ms}, {"d", m.deltaMs}, {"s1", m.corrCh1}, {"s2", m.corrCh2}});
    }
    return QJsonObject{{"count", summary.count},
                       {"minCh1", summary.minCh1}, {"maxCh1", summary.maxCh1}, {"avgCh1", summary.avgCh1}, {"medCh1", summary.medCh1},
                       {"minCh2", summary.minCh2}, {"maxCh2", summary.maxCh2}, {"avgCh2", summary.avgCh2}, {"medCh2", summary.medCh2},
                       {"minDelta", summary.minDelta}, {"maxDelta", summary.maxDelta}, {"avgDelta", summary.avgDelta}, {"medDelta", summary.medDelta},
                       {"samples", arr}};
}


void AudioRouter::addFilePlayer(QString PlayerName, QString File, QString uid)
{
    pjsua_data* intData = pjsua_get_var();
    s_IODevices Audiodevice;
    int slot = PJSUA_INVALID_ID;
    if(uid.isEmpty()) uid = createNewUID();
    QString name = "FP:" + uid + "-File Player " + PlayerName;
    pjmedia_port *player_media_port = nullptr;
    int player_id = createManagedPlayer(name, File, 0, &player_media_port, slot);
    if (player_id == PJSUA_INVALID_ID) {
        m_lib->m_Log->writeLog(1, QString("addFilePlayer: create player failed"));
        return;
    }
    // Default RX level
    int lv = -3;
    pj_status_t status = pjmedia_conf_adjust_rx_level(intData->mconf, slot, dBtoAdjLevel(lv));
    if (status != PJ_SUCCESS) {
        m_lib->m_Log->writeLog(2, QString("addFilePlayer: adjust level failed"));
    }
    Audiodevice.devicetype = FilePlayer;
    Audiodevice.inputname = PlayerName;
    Audiodevice.uid = uid;
    Audiodevice.path = File;
    Audiodevice.portNo.append(slot);
    Audiodevice.PBDevID = player_id;
    Audiodevice.mediaport = player_media_port;
    m_AudioDevices.append(Audiodevice);
    scheduleConferenceRefresh(150);
    m_lib->m_Settings->saveIODevConfig();
    scheduleRouteEvaluation(50);
    emit AudioDevicesChanged(m_AudioDevices);
    return;
}


void AudioRouter::addFileRecorder(QString File, QString uid)
{
    s_IODevices Audiodevice;
    int slot = PJSUA_INVALID_ID;
    if(uid.isEmpty()) uid = createNewUID();
    QString name = "FR:" + uid + "-File Recorder " + File;
    pjmedia_port *media_port = nullptr;
    int rec_id = createManagedRecorder(name, File, &media_port, slot);
    if (rec_id == PJSUA_INVALID_ID) {
        m_lib->m_Log->writeLog(1, QString("addFileRecorder: create failed"));
        return;
    }
    Audiodevice.devicetype = FileRecorder;
    Audiodevice.outputame = name;
    Audiodevice.uid = uid;
    Audiodevice.path = File;
    Audiodevice.portNo.append(slot);
    Audiodevice.RecDevID = rec_id;
    Audiodevice.mediaport = media_port;
    m_AudioDevices.append(Audiodevice);
    m_lib->m_Settings->saveIODevConfig();
    scheduleConferenceRefresh(150);
    emit AudioDevicesChanged(m_AudioDevices);
    return;
}


s_IODevices* AudioRouter::getADeviceByUID(QString uid)
{
    for(auto& device : m_AudioDevices){
        if(device.uid == uid){
            return &device;
        }
    }
    return nullptr;
}

s_audioPortList AudioRouter::listConfPorts(){
    QStringList confportlist;
    s_audioPortList audioPortList;
    pjsua_conf_port_info confinfo;
    pj_status_t status;
    QString debugSlotOut;
    m_srcAudioSlotMap.clear();
    m_destAudioSlotMap.clear();
    pjsua_conf_port_id confports[PJSUA_MAX_CONF_PORTS];
    unsigned port_cnt=PJ_ARRAY_SIZE(confports);
    status = pjsua_enum_conf_ports(confports,&port_cnt);
    if (status != PJ_SUCCESS){
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(2,(QString("ListConfPorts: reading conf port list failed: ") + buf));
    }
    // Determine per-call announcement player and per-call recorder slots
    QSet<int> playerSlots;   // per-call announcement players (useful on TX/src side)
    QSet<int> recorderSlots; // per-call recorders (useful on RX/dest side)
    QHash<int, QString> playerSrcLabel;   // override source label for announcement player
    QHash<int, QString> recorderDstLabel; // override dest label for call recorder
    if (m_lib && m_lib->m_Accounts) {
        const QList<s_account>* accs = m_lib->m_Accounts->getAccounts();
        for (const s_account &acc : *accs) {
            const QString parentKey = QString("ACC:%1").arg(acc.uid);
            for (const s_Call &c : acc.CallList) {
                if (c.player_id != PJSUA_INVALID_ID) {
                    int pslot = getPlayerForParent(parentKey);
                    if (pslot != PJSUA_INVALID_ID) {
                        playerSlots.insert(pslot);
                        QString remoteNum = c.ConnectedTo;
                        int idxColon = remoteNum.indexOf(":");
                        int idxAt = remoteNum.indexOf("@");
                        if (idxColon >= 0 && idxAt > idxColon) {
                            remoteNum = remoteNum.mid(idxColon+1, idxAt - idxColon - 1);
                        }
                        playerSrcLabel[pslot] = QString("Anouncment %1 %2 (CID: %3)").arg(acc.name, remoteNum).arg(c.callId);
                    }
                }
                if (c.rec_id != PJSUA_INVALID_ID) {
                    int rslot = getRecorderForParent(parentKey);
                    if (rslot != PJSUA_INVALID_ID) {
                        recorderSlots.insert(rslot);
                        QString remoteNum = c.ConnectedTo;
                        int idxColon = remoteNum.indexOf(":");
                        int idxAt = remoteNum.indexOf("@");
                        if (idxColon >= 0 && idxAt > idxColon) {
                            remoteNum = remoteNum.mid(idxColon+1, idxAt - idxColon - 1);
                        }
                        recorderDstLabel[rslot] = QString("Callrecorder %1 %2 (CID: %3)").arg(acc.name, remoteNum).arg(c.callId);
                    }
                }
            }
        }
    }

    for (unsigned int i=0;i<port_cnt;i++){
        s_audioPort src, dest;
        int slot = confports[i];
        pjsua_conf_get_port_info(slot, &confinfo);
        QString portName = pj2Str(confinfo.name);
        QStringList split = portName.split("-");
        if(portName.startsWith("AD:")){
            QString uid = split[0].remove("AD:");
            const s_IODevices* aDevice = getADeviceByUID(uid);
            if(aDevice != nullptr){
                if(aDevice->devicetype == TestToneGenerator){
                    src.pjName = confinfo.name;
                    if(m_customSourceLabels.contains(pj2Str(src.pjName))){
                        src.name = m_customSourceLabels[pj2Str(src.pjName)];
                    }
                    else{
                        src.name = aDevice->inputname;
                    }
                    src.slot = slot;
                    src.channelIndex = 1;
                    audioPortList.srcPorts.append(src);
                    m_srcAudioSlotMap[slot] = pj2Str(confinfo.name);
                } else {
                    QString channelString = split.at(1);
                    uint channel = channelString.remove("Ch:").toUInt();
                    if(channel <= aDevice->inChannelCount) {
                        src.pjName = confinfo.name;
                        if(m_customSourceLabels.contains(pj2Str(src.pjName))){
                            src.name = m_customSourceLabels[pj2Str(src.pjName)];
                        }
                        else{
                            src.name = aDevice->inputname + " " + split.at(1);
                        }
                        src.slot = slot;
                        src.channelIndex = (int)channel;
                        audioPortList.srcPorts.append(src);
                        m_srcAudioSlotMap[slot] = pj2Str(confinfo.name);
                    }
                    if(channel <= aDevice->outChannelCount) {
                        dest.pjName = confinfo.name;
                        if(m_customDestLabels.contains(pj2Str(dest.pjName))){
                            dest.name = m_customDestLabels[pj2Str(dest.pjName)];
                        }
                        else{
                            dest.name = aDevice->outputame + " " + split.at(1);
                        }
                        dest.slot = slot;
                        dest.channelIndex = (int)channel;
                        audioPortList.destPorts.append(dest);
                        m_destAudioSlotMap[slot] = pj2Str(confinfo.name);
                    }
                }
            }
        } else if(portName.startsWith("WRTC:")){
            // WebRTC channel splitter/combiner ports are named as WRTC:<channelId>-<sessionId>-Ch:<n>
            QString id = split[0];
            id.remove("WRTC:");
            bool wrtcSendOnly = false;
            if (m_lib && m_lib->m_WebRTCChannels) {
                QList<s_webrtc_channel>* chans = m_lib->m_WebRTCChannels->getChannels();
                for (const s_webrtc_channel &wch : *chans) {
                    if (wch.id == id) { wrtcSendOnly = wch.sendOnly; break; }
                }
            }
            src.pjName = confinfo.name;
            if(m_customSourceLabels.contains(pj2Str(src.pjName))){
                src.name = m_customSourceLabels[pj2Str(src.pjName)];
            } else {
                src.name = QString("WRTC: ") + id + " " + split.value(1);
            }
            src.slot = slot;
            src.channelIndex = getChannelIndexForSlot(slot);
            dest.pjName = confinfo.name;
            if(m_customDestLabels.contains(pj2Str(dest.pjName))){
                dest.name = m_customDestLabels[pj2Str(dest.pjName)];
            } else {
                dest.name = QString("WRTC: ") + id + " " + split.value(1);
            }
            dest.slot = slot;
            dest.channelIndex = getChannelIndexForSlot(slot);
            // Enrich with parent metadata if known
            if (m_slotToParentKey.contains(slot)) {
                src.parentKey = m_slotToParentKey.value(slot); src.entityType = Entity_WebRTCChannel;
                dest.parentKey = src.parentKey; dest.entityType = Entity_WebRTCChannel;
            }
            if (wrtcSendOnly) {
                audioPortList.destPorts.append(dest);
            } else {
                audioPortList.srcPorts.append(src);
                audioPortList.destPorts.append(dest);
            }
            m_srcAudioSlotMap[slot] = pj2Str(confinfo.name);
            m_destAudioSlotMap[slot] = pj2Str(confinfo.name);
        } else if(portName.startsWith("SIP:")){
            // SIP per-call ports are named as SIP:<accountName>-<remoteNumber>-Ch:<n>
            QString accName = split[0];
            accName.remove("SIP:");
            QString remote = split.value(1);
            QString chLbl = split.value(2); // e.g., Ch:1
            src.pjName = confinfo.name;
            if(m_customSourceLabels.contains(pj2Str(src.pjName))){
                src.name = m_customSourceLabels[pj2Str(src.pjName)];
            } else {
                src.name = QString("SIP: ") + accName + " " + remote + (chLbl.isEmpty() ? "" : (" " + chLbl));
            }
            src.slot = slot;
            src.channelIndex = getChannelIndexForSlot(slot);
            dest.pjName = confinfo.name;
            if(m_customDestLabels.contains(pj2Str(dest.pjName))){
                dest.name = m_customDestLabels[pj2Str(dest.pjName)];
            } else {
                dest.name = QString("SIP: ") + accName + " " + remote + (chLbl.isEmpty() ? "" : (" " + chLbl));
            }
            dest.slot = slot;
            dest.channelIndex = getChannelIndexForSlot(slot);
            // If this slot belongs to a specific call, append the callId to disambiguate
            if (m_lib && m_lib->m_Accounts) {
                const QList<s_account>* accs = m_lib->m_Accounts->getAccounts();
                for (const s_account &acc : *accs) {
                    if (acc.name == accName) {
                    for (const s_Call &c : acc.CallList) {
                            if (!c.ConnectedTo.isEmpty() && c.ConnectedTo.contains(remote)) {
                            src.name += QString(" (CID: %1)").arg(c.callId);
                            dest.name += QString(" (CID: %1)").arg(c.callId);
                                break;
                        }
                    }
                        break;
                }
            }
            }
            if (m_slotToParentKey.contains(slot)) {
                src.parentKey = m_slotToParentKey.value(slot); src.entityType = Entity_SipAccount;
                dest.parentKey = src.parentKey; dest.entityType = Entity_SipAccount;
            }
            audioPortList.srcPorts.append(src);
            audioPortList.destPorts.append(dest);
            m_srcAudioSlotMap[slot] = pj2Str(confinfo.name);
            m_destAudioSlotMap[slot] = pj2Str(confinfo.name);
        } else if(portName.startsWith("FP:")){
            QString uid = split[0].remove("FP:");
            const s_IODevices* aDevice = getADeviceByUID(uid);
            if(aDevice != nullptr) {
                if(aDevice->devicetype == FilePlayer){
                    src.pjName = confinfo.name;
                    if(m_customSourceLabels.contains(pj2Str(src.pjName))){
                        src.name = m_customSourceLabels[pj2Str(src.pjName)];
                    }
                    else{
                        src.name = "File Player: " + aDevice->inputname;
                    }
                    src.slot = slot;
                    src.channelIndex = getChannelIndexForSlot(slot);
                    audioPortList.srcPorts.append(src);
                    m_srcAudioSlotMap[slot] = pj2Str(confinfo.name);
                }
            }
        }
        else if(portName.startsWith("FR:")){
            QString uid = split[0].remove("FR:");
            const s_IODevices* aDevice = getADeviceByUID(uid);
            if(aDevice != nullptr) {
                if(aDevice->devicetype == FileRecorder){
                    dest.pjName = confinfo.name;
                    if(m_customDestLabels.contains(pj2Str(dest.pjName))){
                        dest.name = m_customDestLabels[pj2Str(dest.pjName)];
                    }
                    else{
                        dest.name = aDevice->outputame;
                    }
                    dest.slot = slot;
                    dest.channelIndex = getChannelIndexForSlot(slot);
                    audioPortList.destPorts.append(dest);
                    m_destAudioSlotMap[slot] = pj2Str(confinfo.name);
                }
            }
        } else if (portName.startsWith("LM:")) {
            // Expected format set in attachStreamChannels: LM:<accountName>-Ch:<n>
            // Our accountName is "LatencyMon-<uid8>", extract uid8 for display
            // Examples: "LM:LatencyMon-e9d96708-Ch:1"
            // Build src/dest labels: "Latency Monitor <uid8> Ch:<n>"
            QString uid8;
            int idxDash = portName.indexOf("-");
            int idxCh = portName.indexOf("-Ch:");
            if (idxDash >= 0 && idxCh > idxDash) {
                uid8 = portName.mid(idxDash+1, idxCh - (idxDash+1));
            }
            if (uid8.startsWith("LatencyMon-")) uid8 = uid8.mid(QString("LatencyMon-").size());
            QString chLabel = portName.mid(idxCh + 1); // e.g., "Ch:1"
            QString nice = QString("Latency Monitor %1 %2").arg(uid8, chLabel);
            src.name = nice;
            dest.name = nice;
            src.slot = slot;
            dest.slot = slot;
            audioPortList.srcPorts.append(src);
            audioPortList.destPorts.append(dest);
            m_srcAudioSlotMap[slot] = src.name;
            m_destAudioSlotMap[slot] = dest.name;
            continue;
        } else {
            // Fallback: append any unknown/other conf ports as generic entries
            src.pjName = confinfo.name;
            if(m_customSourceLabels.contains(pj2Str(src.pjName))){
                src.name = m_customSourceLabels[pj2Str(src.pjName)];
            } else {
                src.name = portName;
            }
            src.slot = slot;
            src.channelIndex = getChannelIndexForSlot(slot);
            dest.pjName = confinfo.name;
            if(m_customDestLabels.contains(pj2Str(dest.pjName))){
                dest.name = m_customDestLabels[pj2Str(dest.pjName)];
            } else {
                dest.name = portName;
            }
            dest.slot = slot;
            dest.channelIndex = getChannelIndexForSlot(slot);
            if (m_slotToParentKey.contains(slot)) {
                src.parentKey = m_slotToParentKey.value(slot);
                dest.parentKey = src.parentKey;
            }
            if (!recorderSlots.contains(slot))
            {
                if (playerSrcLabel.contains(slot)) src.name = playerSrcLabel.value(slot);
                audioPortList.srcPorts.append(src);
            }
            if (!playerSlots.contains(slot))
            {
                if (recorderDstLabel.contains(slot)) dest.name = recorderDstLabel.value(slot);
                audioPortList.destPorts.append(dest);
            }
            m_srcAudioSlotMap[slot] = pj2Str(confinfo.name);
            m_destAudioSlotMap[slot] = pj2Str(confinfo.name);
        }
        debugSlotOut.clear();
        debugSlotOut.append(QString::number(slot));
        debugSlotOut.append(": ");
        debugSlotOut.append(confinfo.name.ptr);
        confportlist.append(debugSlotOut);
    }
    // Append virtual parents (accounts and WebRTC channels), stereo (Ch:1, Ch:2) on both directions
    if (m_lib && m_lib->m_Accounts) {
        const QList<s_account>* accs = m_lib->m_Accounts->getAccounts();
        for (const s_account &acc : *accs) {
            const QString key = QString("ACC:%1").arg(acc.uid);
            for (int ch=1; ch<=2; ++ch) {
                s_audioPort parentSrc, parentDst; parentSrc.isVirtual = true; parentDst.isVirtual = true;
                parentSrc.entityType = Entity_SipAccount; parentDst.entityType = Entity_SipAccount;
                parentSrc.parentKey = key; parentDst.parentKey = key;
                parentSrc.name = QString("Account: %1 Ch:%2").arg(acc.name).arg(ch);
                parentDst.name = parentSrc.name;
                parentSrc.slot = getOrCreateVirtualSlot(key, Entity_SipAccount, true, ch);
                parentDst.slot = getOrCreateVirtualSlot(key, Entity_SipAccount, false, ch);
                parentSrc.channelIndex = ch;
                parentDst.channelIndex = ch;
                parentSrc.childSlots = m_parentKeyToChildSlots.value(key);
                parentDst.childSlots = parentSrc.childSlots;
                audioPortList.srcPorts.append(parentSrc);
                audioPortList.destPorts.append(parentDst);
            }
        }
    }
    if (m_lib && m_lib->m_WebRTCChannels) {
        QList<s_webrtc_channel>* chans = m_lib->m_WebRTCChannels->getChannels();
        for (const s_webrtc_channel &ch : *chans) {
            const QString key = QString("WRTC_CH:%1").arg(ch.id);
            const QString disp = ch.description.isEmpty()? ch.id : ch.description;
            for (int chix=1; chix<=2; ++chix) {
                s_audioPort parentSrc, parentDst; parentSrc.isVirtual = true; parentDst.isVirtual = true;
                parentSrc.entityType = Entity_WebRTCChannel; parentDst.entityType = Entity_WebRTCChannel;
                parentSrc.parentKey = key; parentDst.parentKey = key;
                parentSrc.name = QString("WebRTC: %1 Ch:%2").arg(disp).arg(chix);
                parentDst.name = parentSrc.name;
                parentSrc.slot = getOrCreateVirtualSlot(key, Entity_WebRTCChannel, true, chix);
                parentDst.slot = getOrCreateVirtualSlot(key, Entity_WebRTCChannel, false, chix);
                parentSrc.channelIndex = chix;
                parentDst.channelIndex = chix;
                parentSrc.childSlots = m_parentKeyToChildSlots.value(key);
                parentDst.childSlots = parentSrc.childSlots;
                if (ch.sendOnly) {
                    // Send-only channel: expose only TX side (dest)
                    audioPortList.destPorts.append(parentDst);
                } else {
                    audioPortList.srcPorts.append(parentSrc);
                    audioPortList.destPorts.append(parentDst);
                }
            }
        }
    }
    // Also register virtual self-slots only for AudioDevices (sound cards) so routes remain visible when offline
    for (const s_IODevices &dev : m_AudioDevices) {
        if (dev.devicetype == SoundDevice) {
            int maxCh = qMax(dev.inChannelCount, dev.outChannelCount);
            if (maxCh <= 0) maxCh = 2;
            for (int ch = 1; ch <= maxCh; ++ch) {
                QString nm = QString("AD:%1-Ch:%2").arg(dev.uid).arg(ch);
                if (!m_srcAudioSlotMap.values().contains(nm)) {
                    int vsrc = getOrCreateVirtualDeviceSlot(nm, true);
                    m_srcAudioSlotMap[vsrc] = nm;
                }
                if (!m_destAudioSlotMap.values().contains(nm)) {
                    int vdst = getOrCreateVirtualDeviceSlot(nm, false);
                    m_destAudioSlotMap[vdst] = nm;
                }
            }
        }
    }
    return audioPortList;
}

int AudioRouter::getOrCreateVirtualDeviceSlot(const QString &devName, bool isSource)
{
    // devName example: AD:<uid>-Ch:<n> or FP:<uid>-<name> or FR:<uid>-...
    const QString vkey = QString("DEV|%1|%2").arg(devName).arg(isSource?"SRC":"DST");
    if (m_virtualKeyToSlot.contains(vkey)) return m_virtualKeyToSlot.value(vkey);
    int hashed = (int)qHash(vkey);
    if (hashed == 0) hashed = 1;
    int vslot = - (qAbs(hashed) % 100000) - 50000; // separate range from parent virtuals
    m_virtualKeyToSlot[vkey] = vslot;
    m_virtualDeviceSlotToName[vslot] = devName;
    return vslot;
}

int AudioRouter::getOrCreateVirtualSlot(const QString &parentKey, AudioEntityType type, bool isSource, int channelIndex)
{
    const QString vkey = parentKey + "|" + (isSource? "SRC":"DST") + "|" + QString::number(channelIndex);
    if (m_virtualKeyToSlot.contains(vkey)) return m_virtualKeyToSlot.value(vkey);
    // Deterministic negative slot per vkey, stable across runs
    int hashed = (int)qHash(vkey);
    if (hashed == 0) hashed = 1;
    int vslot = - (qAbs(hashed) % 100000) - 10; // keep away from -1
    m_virtualKeyToSlot[vkey] = vslot;
    m_virtualSlotToParentKey[vslot] = parentKey;
    m_virtualSlotToEntityType[vslot] = type;
    m_virtualSlotToChannel[vslot] = channelIndex;
    m_virtualSlotIsSource[vslot] = isSource;
    return vslot;
}

int AudioRouter::connectConfPort(int src_slot, int sink_slot, int level, bool persistant)
{
    if (src_slot == PJSUA_INVALID_ID || sink_slot == PJSUA_INVALID_ID) {
        return PJ_EINVAL;
    }
    // Log API request with human-readable labels, including virtual parents/devices
    auto labelForSlot = [&](int slot, bool isSource)->QString{
        if (slot >= 0) {
            return isSource ? m_srcAudioSlotMap.value(slot) : m_destAudioSlotMap.value(slot);
        }
        // Parent virtual?
        if (m_virtualSlotToParentKey.contains(slot)) {
            QString pk = m_virtualSlotToParentKey.value(slot);
            int ch = m_virtualSlotToChannel.value(slot, 0);
            return QString("%1 Ch:%2").arg(pk).arg(ch);
        }
        // Device virtual?
        if (m_virtualDeviceSlotToName.contains(slot)) {
            return m_virtualDeviceSlotToName.value(slot);
        }
        return QString("vslot:%1").arg(slot);
    };
    const QString sLabel = labelForSlot(src_slot, true);
    const QString dLabel = labelForSlot(sink_slot, false);
    m_lib->m_Log->writeLog(3, QString("AudioRouter connectConfPort: %1 -> %2 @ %3 dB (persistant=%4) [%5 -> %6]")
                               .arg(src_slot).arg(sink_slot).arg(level).arg(persistant)
                               .arg(sLabel.isEmpty()? "?" : sLabel)
                               .arg(dLabel.isEmpty()? "?" : dLabel));

    // Virtual to real routing support: map negative "virtual" slots to parent routing
    if (src_slot < 0 || sink_slot < 0) {
        int ret = PJ_EINVAL;
        // src virtual parent → device
        if (src_slot < 0 && sink_slot >= 0) {
            const QString pkey = m_virtualSlotToParentKey.value(src_slot);
            const QString otherDev = m_destAudioSlotMap.value(sink_slot);
            const int ch = m_virtualSlotToChannel.value(src_slot, 0);
            if (!pkey.isEmpty() && !otherDev.isEmpty()) {
                ret = connectParentToDev(pkey, m_virtualSlotToEntityType.value(src_slot, Entity_Unknown), true, otherDev, level, persistant, ch, 0);
            }
            return ret;
        }
        // device → dest virtual parent
        if (src_slot >= 0 && sink_slot < 0) {
            const QString pkey = m_virtualSlotToParentKey.value(sink_slot);
            const QString otherDev = m_srcAudioSlotMap.value(src_slot);
            const int ch = m_virtualSlotToChannel.value(sink_slot, 0);
            if (!pkey.isEmpty() && !otherDev.isEmpty()) {
                ret = connectParentToDev(pkey, m_virtualSlotToEntityType.value(sink_slot, Entity_Unknown), false, otherDev, level, persistant, ch, 0);
            }
            return ret;
        }
        // parent ↔ parent via virtual IDs
        if (src_slot < 0 && sink_slot < 0) {
            const QString lkey = m_virtualSlotToParentKey.value(src_slot);
            const QString rkey = m_virtualSlotToParentKey.value(sink_slot);
            const int lch = m_virtualSlotToChannel.value(src_slot, 0);
            const int rch = m_virtualSlotToChannel.value(sink_slot, 0);
            if (!lkey.isEmpty() && !rkey.isEmpty()) {
                ret = connectParentToDev(lkey, m_virtualSlotToEntityType.value(src_slot, Entity_Unknown), true, rkey, level, persistant, lch, rch);
            }
            return ret;
        }
        // Fallback safety
        m_lib->m_Log->writeLog(2, QString("connectConfPort: invalid slot(s): ") + QString::number(src_slot) + " -> " + QString::number(sink_slot));
        return ret;
    }
    s_audioRoutes route;
    route.srcSlot = src_slot;
    route.destSlot = sink_slot;
    route.srcDevName = m_srcAudioSlotMap[src_slot];
    route.destDevName = m_destAudioSlotMap[sink_slot];
    route.level = level;
    route.persistant = persistant;
    pj_status_t status;
    pjsua_conf_port_id src = src_slot;
    pjsua_conf_port_id sink = sink_slot;
    int leveladjust = dBtoAdjLevel(level);
    pjsua_data* intData = pjsua_get_var();

    // Guard: skip duplicate connect if already present
    bool already = false;
    {
        pjsua_data* chk = pjsua_get_var();
        if (chk && chk->mconf) {
            pjmedia_conf_port_info cpi; if (pjmedia_conf_get_port_info(chk->mconf, src, &cpi) == PJ_SUCCESS) {
                if (cpi.listener_slots && cpi.listener_cnt > 0) {
                    for (unsigned j=0;j<cpi.listener_cnt;++j) if ((int)cpi.listener_slots[j]==sink) { already=true; break; }
                }
            }
        }
    }
    if (!already) status = pjmedia_conf_connect_port(intData->mconf, src, sink, leveladjust);
    else status = PJ_SUCCESS;
    if (status == PJ_SUCCESS){
        m_lib->m_Log->writeLog(3,(QString("connect slot: ") + QString::number(src_slot) + " to " + QString::number(sink_slot) + " successfully" ));
        // Record/append route if not present
        bool routeExists = false;
        for(const auto& existingroute : m_audioRoutes){
            if(existingroute.srcSlot == route.srcSlot && existingroute.destSlot == route.destSlot){
                routeExists = true;
                break;
            }
        }
        if(!routeExists) m_audioRoutes.append(route);
        // Debounce reconcile to avoid races with other triggers, but update UI immediately
        scheduleRouteEvaluation(50);
        emitRoutesChanged();
        if(persistant)
            m_lib->m_Settings->saveAudioRoutes();
    }
    else{
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(2,(QString("ConnectConfPort: connecting slots failed: ") + buf));
    }
    return status;
}


int AudioRouter::disconnectConfPort(int src_slot, int sink_slot)
{
    if (src_slot == PJSUA_INVALID_ID || sink_slot == PJSUA_INVALID_ID) {
        return PJ_SUCCESS;
    }
    // Log API request with human-readable labels, including virtual parents/devices
    auto labelForSlot = [&](int slot, bool isSource)->QString{
        if (slot >= 0) {
            return isSource ? m_srcAudioSlotMap.value(slot) : m_destAudioSlotMap.value(slot);
        }
        if (m_virtualSlotToParentKey.contains(slot)) {
            QString pk = m_virtualSlotToParentKey.value(slot);
            int ch = m_virtualSlotToChannel.value(slot, 0);
            return QString("%1 Ch:%2").arg(pk).arg(ch);
        }
        if (m_virtualDeviceSlotToName.contains(slot)) {
            return m_virtualDeviceSlotToName.value(slot);
        }
        return QString("vslot:%1").arg(slot);
    };
    const QString sLabel = labelForSlot(src_slot, true);
    const QString dLabel = labelForSlot(sink_slot, false);
    m_lib->m_Log->writeLog(3, QString("AudioRouter disconnectConfPort: %1 -> %2 [%3 -> %4]")
                               .arg(src_slot).arg(sink_slot)
                               .arg(sLabel.isEmpty()? "?" : sLabel)
                               .arg(dLabel.isEmpty()? "?" : dLabel));

    // Handle virtual parent routing removal without touching pjmedia
    if (src_slot < 0 || sink_slot < 0) {
        QString lkey = (src_slot < 0) ? m_virtualSlotToParentKey.value(src_slot) : QString();
        QString rkey = (sink_slot < 0) ? m_virtualSlotToParentKey.value(sink_slot) : QString();
        QString srcDev = (src_slot >= 0) ? m_srcAudioSlotMap.value(src_slot) : QString();
        QString dstDev = (sink_slot >= 0) ? m_destAudioSlotMap.value(sink_slot) : QString();
        int lch = (src_slot < 0) ? m_virtualSlotToChannel.value(src_slot, 0) : 0;
        int rch = (sink_slot < 0) ? m_virtualSlotToChannel.value(sink_slot, 0) : 0;

        // New: handle anchored device virtual slots (child-only devices)
        QString vSrcDev = m_virtualDeviceSlotToName.contains(src_slot) ? m_virtualDeviceSlotToName.value(src_slot) : QString();
        QString vDstDev = m_virtualDeviceSlotToName.contains(sink_slot) ? m_virtualDeviceSlotToName.value(sink_slot) : QString();
        if (!vSrcDev.isEmpty() || !vDstDev.isEmpty()) {
            bool save = false; bool removed = false; bool changed = false;
            QMutableListIterator<s_audioRoutes> it(m_audioRoutes);
            while (it.hasNext()) {
                s_audioRoutes &r = it.next();
                // device(virtual src) -> real/virtual dest
                if (!vSrcDev.isEmpty()) {
                    if (!r.srcIsParent && r.srcDevName == vSrcDev) {
                        bool destMatch = false;
                        if (sink_slot >= 0) {
                            // match by real dest slot or by resolved name
                            destMatch = (r.destSlot == sink_slot) || (!r.destIsParent && r.destDevName == dstDev);
                        } else if (!vDstDev.isEmpty()) {
                            destMatch = (!r.destIsParent && r.destDevName == vDstDev);
                        }
                        if (destMatch) { save = save || r.persistant; it.remove(); removed = true; continue; }
                    }
                }
                // real/virtual src -> device(virtual dest)
                if (!vDstDev.isEmpty()) {
                    if (!r.destIsParent && r.destDevName == vDstDev) {
                        bool srcMatch = false;
                        if (src_slot >= 0) {
                            srcMatch = (r.srcSlot == src_slot) || (!r.srcIsParent && r.srcDevName == srcDev);
                        } else if (!vSrcDev.isEmpty()) {
                            srcMatch = (!r.srcIsParent && r.srcDevName == vSrcDev);
                        }
                        if (srcMatch) { save = save || r.persistant; it.remove(); removed = true; continue; }
                    }
                }
            }
            if (save) m_lib->m_Settings->saveAudioRoutes();
            if (removed || changed) {
                reconcileActiveRoutesNow();
            }
            return removed ? PJ_SUCCESS : PJ_EINVAL;
        }

        bool save = false; bool removed = false;
        bool childRemoved = false; bool changed = false;
        QMutableListIterator<s_audioRoutes> it(m_audioRoutes);
        while (it.hasNext()) {
            s_audioRoutes &r = it.next();
            // parent->device
            if (src_slot < 0 && sink_slot >= 0 && r.srcIsParent && r.srcParentKey == lkey && (r.srcParentChannel == 0 || r.srcParentChannel == lch) && r.destDevName == dstDev) {
                save = save || r.persistant; it.remove(); removed = true; continue;
            }
            // device->parent
            if (src_slot >= 0 && sink_slot < 0 && r.destIsParent && r.destParentKey == rkey && (r.destParentChannel == 0 || r.destParentChannel == rch) && r.srcDevName == srcDev) {
                save = save || r.persistant; it.remove(); removed = true; continue;
            }
            // parent->parent
            if (src_slot < 0 && sink_slot < 0 && r.srcIsParent && r.destIsParent && r.srcParentKey == lkey && r.destParentKey == rkey
                && (r.srcParentChannel == 0 || r.srcParentChannel == lch)
                && (r.destParentChannel == 0 || r.destParentChannel == rch)) {
                save = save || r.persistant; it.remove(); removed = true; continue;
            }
        }

        // Helper: recorder conf slot for a given child slot using stored parent mapping (left channel only)
        auto recorderSlotForChild = [&](int childSlot) -> int {
            int ch = getChannelIndexForSlot(childSlot);
            if (ch != 1) return -1;
            QString pkey = getParentKeyForSlot(childSlot);
            if (pkey.isEmpty()) return -1;
            int r = getRecorderForParent(pkey);
            return (r == PJSUA_INVALID_ID ? -1 : r);
        };

        // Also disconnect any ephemeral child-level routes created by these parent routes (including recorder mirrors)
        pjsua_data* intData = pjsua_get_var();
        if (src_slot < 0 && sink_slot >= 0 && !lkey.isEmpty() && !dstDev.isEmpty()) {
            const QList<int> children = m_parentKeyToChildSlots.value(lkey);
            const QMap<int, QString> srcMap = getSrcAudioSlotMap();
            for (int child : children) {
                QMutableListIterator<s_audioRoutes> di(m_audioRoutes);
                while (di.hasNext()) {
                    s_audioRoutes &dr = di.next();
                    // Remove only child connections that match the source channel (lch), and also any child connection pointing to another parent's children (parent->child)
                    const QString childName = srcMap.value(child);
                    bool channelMatch = (lch == 0) || (!childName.isEmpty() && childName.contains(QString("Ch:%1").arg(lch)));
                    bool isChildToDev = (dr.srcSlot == child && (dr.destDevName == dstDev || dr.destSlot == sink_slot));
                    // Also cover parent->parent shadow fanned to a child's destination (remove if child belongs to other parent in this removal)
                    if (channelMatch && !dr.srcIsParent && !dr.destIsParent && dr.persistant == false && isChildToDev) {
                        if (intData && intData->mconf) {
                            pjmedia_conf_disconnect_port(intData->mconf, dr.srcSlot, dr.destSlot);
                        }
                        di.remove();
                        childRemoved = true;
                    }
                }
                // Remove recorder mirror only when removing left-channel (Ch:1) mapping
                if (lch == 1) {
                int recSlot = recorderSlotForChild(child);
                if (recSlot >= 0 && intData && intData->mconf) {
                    pjmedia_conf_disconnect_port(intData->mconf, child, recSlot);
                    // Purge temporary route bookkeeping
                    QMutableListIterator<s_audioRoutes> ri(m_audioRoutes);
                    while (ri.hasNext()) {
                        s_audioRoutes &rr = ri.next();
                        if (!rr.srcIsParent && !rr.destIsParent && rr.persistant == false && rr.srcSlot == child && rr.destSlot == recSlot) {
                            ri.remove();
                            childRemoved = true;
                            }
                        }
                    }
                }
            }
        } else if (src_slot >= 0 && sink_slot < 0 && !rkey.isEmpty() && !srcDev.isEmpty()) {
            const QList<int> children = m_parentKeyToChildSlots.value(rkey);
            const QMap<int, QString> dstMap = getDestAudioSlotMap();
            for (int child : children) {
                QMutableListIterator<s_audioRoutes> di(m_audioRoutes);
                while (di.hasNext()) {
                    s_audioRoutes &dr = di.next();
                    const QString childName = dstMap.value(child);
                    bool channelMatch = (rch == 0) || (!childName.isEmpty() && childName.contains(QString("Ch:%1").arg(rch)));
                    if (channelMatch && !dr.srcIsParent && !dr.destIsParent && dr.persistant == false && dr.destSlot == child && (dr.srcDevName == srcDev || dr.srcSlot == src_slot)) {
                        if (intData && intData->mconf) {
                            pjmedia_conf_disconnect_port(intData->mconf, dr.srcSlot, dr.destSlot);
                        }
                        di.remove();
                        childRemoved = true;
                    }
                }
                // Remove recorder mirror for this destination child only when removing left-channel mapping
                if (rch == 1) {
                int recSlot = recorderSlotForChild(child);
                if (recSlot >= 0 && intData && intData->mconf) {
                    pjmedia_conf_disconnect_port(intData->mconf, src_slot, recSlot);
                    QMutableListIterator<s_audioRoutes> ri(m_audioRoutes);
                    while (ri.hasNext()) {
                        s_audioRoutes &rr = ri.next();
                        if (!rr.srcIsParent && !rr.destIsParent && rr.persistant == false && rr.srcSlot == src_slot && rr.destSlot == recSlot) {
                            ri.remove();
                            childRemoved = true;
                            }
                        }
                    }
                }
            }
        } else if (src_slot < 0 && sink_slot < 0 && !lkey.isEmpty() && !rkey.isEmpty()) {
            const QList<int> lchildren = m_parentKeyToChildSlots.value(lkey);
            const QList<int> rchildren = m_parentKeyToChildSlots.value(rkey);
            const QMap<int, QString> srcMap = getSrcAudioSlotMap();
            const QMap<int, QString> dstMap = getDestAudioSlotMap();
            for (int ls : lchildren) {
                for (int rs : rchildren) {
                    QMutableListIterator<s_audioRoutes> di(m_audioRoutes);
                    while (di.hasNext()) {
                        s_audioRoutes &dr = di.next();
                        const QString lsName = srcMap.value(ls);
                        const QString rsName = dstMap.value(rs);
                        bool lmatch = (lch == 0) || (!lsName.isEmpty() && lsName.contains(QString("Ch:%1").arg(lch)));
                        bool rmatch = (rch == 0) || (!rsName.isEmpty() && rsName.contains(QString("Ch:%1").arg(rch)));
                        if (lmatch && rmatch && !dr.srcIsParent && !dr.destIsParent && dr.persistant == false && dr.srcSlot == ls && dr.destSlot == rs) {
                            if (intData && intData->mconf) {
                                pjmedia_conf_disconnect_port(intData->mconf, dr.srcSlot, dr.destSlot);
                            }
                            di.remove();
                            childRemoved = true;
                        }
                    }
                    // Remove recorder mirror for this pair only when removing left destination channel
                    if (rch == 1) {
                    int recSlot = recorderSlotForChild(rs);
                    if (recSlot >= 0 && intData && intData->mconf) {
                        pjmedia_conf_disconnect_port(intData->mconf, ls, recSlot);
                        QMutableListIterator<s_audioRoutes> ri(m_audioRoutes);
                        while (ri.hasNext()) {
                            s_audioRoutes &rr = ri.next();
                            if (!rr.srcIsParent && !rr.destIsParent && rr.persistant == false && rr.srcSlot == ls && rr.destSlot == recSlot) {
                                ri.remove();
                                childRemoved = true;
                                }
                            }
                        }
                    }
                }
            }
            // Additionally: remove any parent->device routes from lkey that target any child device of rkey (covers edge case)
            {
                QMutableListIterator<s_audioRoutes> pi(m_audioRoutes);
                while (pi.hasNext()) {
                    s_audioRoutes &pr = pi.next();
                    if (pr.srcIsParent && pr.srcParentKey == lkey && !pr.destIsParent && !pr.destDevName.isEmpty()) {
                        // If destDevName corresponds to any rchildren slot name (with channel filter), remove it
                        bool matches = false;
                        for (int rs : rchildren) {
                            const QString rsName = dstMap.value(rs);
                            bool rmatch = (rch == 0) || (!rsName.isEmpty() && rsName.contains(QString("Ch:%1").arg(rch)));
                            if (rmatch && rsName == pr.destDevName) { matches = true; break; }
                        }
                        if (matches) { pi.remove(); changed = true; save = save || pr.persistant; }
                    }
                    // Symmetric: device->parent routes landing on lkey and coming from rkey children
                    if (pr.destIsParent && pr.destParentKey == lkey && !pr.srcIsParent && !pr.srcDevName.isEmpty()) {
                        bool matches = false;
                        for (int ls : lchildren) {
                            const QString lsName = srcMap.value(ls);
                            bool lmatch = (lch == 0) || (!lsName.isEmpty() && lsName.contains(QString("Ch:%1").arg(lch)));
                            if (lmatch && lsName == pr.srcDevName) { matches = true; break; }
                        }
                        if (matches) { pi.remove(); changed = true; save = save || pr.persistant; }
                    }
                }
            }
            // If no children exist yet (no call active), still remove the parent->parent route and force reconcile so UI updates
            if (lchildren.isEmpty() || rchildren.isEmpty()) {
                if (save) m_lib->m_Settings->saveAudioRoutes();
                reconcileActiveRoutesNow();
                return PJ_SUCCESS;
            }
        }
        if (save) m_lib->m_Settings->saveAudioRoutes();
        if (removed || childRemoved || changed) {
            reconcileActiveRoutesNow();
        }
        return removed ? PJ_SUCCESS : PJ_EINVAL;
    }

    pj_status_t status;
    pjsua_conf_port_id src = src_slot;
    pjsua_conf_port_id sink = sink_slot;
    pjsua_data* intData = pjsua_get_var();

    // Guard: avoid spurious disconnect errors on non-existent edges
    bool present = false;
    {
        pjsua_data* chk = pjsua_get_var();
        if (chk && chk->mconf) {
            pjmedia_conf_port_info cpi; if (pjmedia_conf_get_port_info(chk->mconf, src, &cpi) == PJ_SUCCESS) {
                if (cpi.listener_slots && cpi.listener_cnt > 0) {
                    for (unsigned j=0;j<cpi.listener_cnt;++j) if ((int)cpi.listener_slots[j]==sink) { present=true; break; }
                }
            }
        }
    }
    status = present ? pjmedia_conf_disconnect_port(intData->mconf, src, sink) : PJ_SUCCESS;
    if (status == PJ_SUCCESS){
        m_lib->m_Log->writeLog(3,(QString("disconnect slot: ") + QString::number(sink_slot) + " to " + QString::number(src_slot) + " successfully" ));
        for(int i = 0; i < m_audioRoutes.size(); ++i){
            if(m_audioRoutes.at(i).srcSlot == src_slot && m_audioRoutes.at(i).destSlot == sink_slot){
                bool persistant = m_audioRoutes.at(i).persistant;
                m_audioRoutes.removeAt(i);
                if(persistant)
                    m_lib->m_Settings->saveAudioRoutes();
            }
        }
        // Also disconnect recorder mirror if sink is a SIP left-channel child with TX+RX recording
        auto recorderSlotForChild = [&](int childSlot) -> int {
            int ch = getChannelIndexForSlot(childSlot);
            if (ch != 1) return -1;
            QString pkey = getParentKeyForSlot(childSlot);
            if (pkey.isEmpty()) return -1;
            int r = getRecorderForParent(pkey);
            return (r == PJSUA_INVALID_ID ? -1 : r);
        };
        int recSlot = recorderSlotForChild(sink_slot);
        if (recSlot >= 0) {
            pjmedia_conf_disconnect_port(intData->mconf, src_slot, recSlot);
            // Purge temporary bookkeeping for mirror
            for (int i = m_audioRoutes.size() - 1; i >= 0; --i) {
                const s_audioRoutes &r = m_audioRoutes.at(i);
                if (!r.srcIsParent && !r.destIsParent && r.persistant == false && r.srcSlot == src_slot && r.destSlot == recSlot) {
                    m_audioRoutes.removeAt(i);
                }
            }
        }
        // Reconcile immediately so ephemeral view updates correctly
        reconcileActiveRoutesNow();
    }
    else{
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(2,(QString("DisonnectConfPort: dissconect slot failed: ") + buf));
    }
    return status;
}

int AudioRouter::removeConfPort(int slot)
{
    if (slot == PJSUA_INVALID_ID || slot < 0) return PJ_EINVAL;
    // Ensure no routes remain
    removeAllRoutesFromSlot(slot);
    // Check if port still exists before removing
    pjsua_conf_port_info pi; pj_status_t st = pjsua_conf_get_port_info(slot, &pi);
    if (st == PJ_SUCCESS) {
        return pjsua_conf_remove_port(slot);
    }
    return PJ_SUCCESS;
}

void AudioRouter::changeConfPortLevel(int src_slot, int sink_slot, int level)
{
    pjsua_data* intData = pjsua_get_var();
    pj_status_t status;
    pjsua_conf_port_id src = src_slot;
    pjsua_conf_port_id sink = sink_slot;
    int leveladjust = dBtoAdjLevel(level);

    status = pjmedia_conf_adjust_conn_level(intData->mconf, src, sink,  leveladjust);
    if (status == PJ_SUCCESS){
        m_lib->m_Log->writeLog(4,(QString("ChangeConfPortLevel: changed level from slot: ") + QString::number(src_slot) + " to " + QString::number(sink_slot) + " successfully" ));
        for(auto& route : m_audioRoutes){
            if(route.srcSlot == src_slot && route.destSlot == sink_slot){
                route.level = level;
                emit confportLevelChanged(route);
                if(route.persistant)
                    m_lib->m_Settings->saveAudioRoutes();
            }
        }
    }
    else{
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(1,(QString("ChangeConfPortLevel: change level failed: ") + buf));
    }
}

void AudioRouter::conferenceBridgeChanged()
{
    m_confPortList = listConfPorts();
    // Merge in any unknown active connections from the bridge as ephemeral entries
    reconcileActiveConnections();
    emitRoutesChanged();
    
    emit audioRoutesTableChanged(m_confPortList);
}

int AudioRouter::createManagedPlayer(const QString &portLabel, const QString &filePath,
                            unsigned playerFlags, pjmedia_port **outMediaPort,
                            int &outConfSlot)
{
    if (filePath.isEmpty()) return PJSUA_INVALID_ID;
    pjsua_data* intData = pjsua_get_var();
    pjsua_player_id player_id; pj_status_t status; pjmedia_port *media_port = nullptr;
    const pj_str_t sound_file = pj_strdup3 (m_lib->pool, filePath.toStdString().c_str());
    status = pjsua_player_create(&sound_file, playerFlags, &player_id);
    if (status != PJ_SUCCESS) return PJSUA_INVALID_ID;
    if (pjsua_player_get_port(player_id, &media_port) != PJ_SUCCESS) return PJSUA_INVALID_ID;
    pj_strdup2(m_lib->pool, &media_port->info.name, portLabel.toStdString().c_str());
    if (player_id >= 0 && (unsigned)player_id < PJ_ARRAY_SIZE(intData->player)) {
        unsigned autoSlot = intData->player[player_id].slot;
        if (autoSlot != PJSUA_INVALID_ID) {
            removeAllRoutesFromSlot((int)autoSlot);
            pjsua_conf_remove_port((int)autoSlot);
            intData->player[player_id].slot = PJSUA_INVALID_ID;
        }
    }
    outConfSlot = PJSUA_INVALID_ID;
    if (pjsua_conf_add_port(m_lib->pool, media_port, &outConfSlot) != PJ_SUCCESS) return PJSUA_INVALID_ID;
    if (outMediaPort) *outMediaPort = media_port;
    return player_id;
}

int AudioRouter::createManagedRecorder(const QString &portLabel, const QString &filePath,
                              pjmedia_port **outMediaPort, int &outConfSlot)
{
    Q_UNUSED(portLabel);
    if (filePath.isEmpty()) return PJSUA_INVALID_ID;
    pjsua_recorder_id rec_id; pj_status_t status; pjmedia_port *media_port = nullptr;
    pj_str_t rec_file = pj_str(const_cast<char*>(filePath.toStdString().c_str()));
    status = pjsua_recorder_create(&rec_file, 0, NULL, 0, 0, &rec_id);
    if (status != PJ_SUCCESS) return PJSUA_INVALID_ID;
    if (pjsua_recorder_get_port(rec_id, &media_port) != PJ_SUCCESS) return PJSUA_INVALID_ID;
    outConfSlot = pjsua_recorder_get_conf_port(rec_id);
    if (outMediaPort) *outMediaPort = media_port;
    return rec_id;
}
bool AudioRouter::reconcileActiveConnections()
{
    bool changed = false;
    pjsua_data* intData = pjsua_get_var();
    if (!intData || !intData->mconf) return false;
    // Ensure slot-name maps are fresh so DevName -> slot lookups succeed
    m_confPortList = listConfPorts();

    // Desired edges built from intent routes and flags
    struct Edge { int src; int dst; int level; };
    auto edgeKey = [](int s, int d){ return QString::number(s)+">"+QString::number(d); };
    QHash<QString, Edge> desired;
    // Reset ephemeral routes list so UI can show derived child routes
    m_ephemeralRoutes.clear();

    const QMap<int, QString> srcMap = getSrcAudioSlotMap();
    const QMap<int, QString> dstMap = getDestAudioSlotMap();

    // Helper: enumerate children for a parent key
    auto childrenFor = [&](const QString &pkey){ return getChildSlotsForParent(pkey); };
    // Helper: recorder slot for a given left-channel call child (using registry)
    auto recorderForChild = [&](int childSlot)->int{
        int ch = getChannelIndexForSlot(childSlot);
        if (ch != 1) return -1;
        QString parentKey = getParentKeyForSlot(childSlot);
        if (parentKey.isEmpty()) return -1;
        int r = getRecorderForParent(parentKey);
        return (r == PJSUA_INVALID_ID ? -1 : r);
    };

    // Build desired from m_audioRoutes (resolve parents/devices)
    for (const s_audioRoutes &r : m_audioRoutes) {
        // Compute candidate source slots
        QList<int> srcSlots;
        if (r.srcIsParent) {
            for (int s : childrenFor(r.srcParentKey)) {
                int sch = getChannelIndexForSlot(s);
                if (r.srcParentChannel>0 && sch>0 && sch!=r.srcParentChannel) continue;
                srcSlots.append(s);
            }
        } else if (r.srcSlot >= 0) {
            // If stored slot is not currently active, fall back to name/virtual
            if (m_srcAudioSlotMap.contains(r.srcSlot)) {
                srcSlots.append(r.srcSlot);
            } else if (!r.srcDevName.isEmpty()) {
                int s = m_srcAudioSlotMap.key(r.srcDevName, PJSUA_INVALID_ID);
                if (s>=0) srcSlots.append(s);
                else {
                    int vs = getOrCreateVirtualDeviceSlot(r.srcDevName, true);
                    srcSlots.append(vs);
                }
            }
        } else if (!r.srcDevName.isEmpty()) {
            int s = m_srcAudioSlotMap.key(r.srcDevName, PJSUA_INVALID_ID);
            if (s>=0) srcSlots.append(s);
            else {
                // Use anchored virtual slot for offline child-only device sources
                int vs = getOrCreateVirtualDeviceSlot(r.srcDevName, true);
                srcSlots.append(vs);
            }
        }

        // Compute candidate dest slots
        QList<int> dstSlots;
        if (r.destIsParent) {
            for (int d : childrenFor(r.destParentKey)) {
                int dch = getChannelIndexForSlot(d);
                if (r.destParentChannel>0 && dch>0 && dch!=r.destParentChannel) continue;
                dstSlots.append(d);
            }
        } else if (r.destSlot >= 0) {
            // If stored slot is not currently active, fall back to name/virtual
            if (m_destAudioSlotMap.contains(r.destSlot)) {
                dstSlots.append(r.destSlot);
            } else if (!r.destDevName.isEmpty()) {
                int d = m_destAudioSlotMap.key(r.destDevName, PJSUA_INVALID_ID);
                if (d>=0) dstSlots.append(d);
                else {
                    int vd = getOrCreateVirtualDeviceSlot(r.destDevName, false);
                    dstSlots.append(vd);
                }
            }
        } else if (!r.destDevName.isEmpty()) {
            int d = m_destAudioSlotMap.key(r.destDevName, PJSUA_INVALID_ID);
            if (d>=0) dstSlots.append(d);
            else {
                // Use anchored virtual slot for offline child-only device destinations
                int vd = getOrCreateVirtualDeviceSlot(r.destDevName, false);
                dstSlots.append(vd);
            }
        }

        for (int s : srcSlots) for (int d : dstSlots) {
            // Only connect in pjmedia for real slots (>=0); keep virtuals for UI state only
            if (s>=0 && d>=0) {
                desired.insert(edgeKey(s,d), Edge{s,d,r.level});
            }
            // Emit derived child route only when the original uses parents; always non-persistent
            if (r.srcIsParent || r.destIsParent) {
                s_audioRoutes er; er.srcSlot=s; er.destSlot=d; er.level=r.level; er.persistant=false;
                m_ephemeralRoutes.append(er);
            }
            // Recorder mirroring if flagged and destination is an account
            if (r.mirrorToRecorder && r.destIsParent && r.destParentKey.startsWith("ACC:")) {
                // Defer recorder edges while announcement player is active for this parent
                if (m_parentToPlayerSlot.contains(r.destParentKey)) {
                    // skip mirroring until EOF clears the player and triggers reconcile
                } else {
                // Mirror only to specified dest channel (default 1)
                int dch = getChannelIndexForSlot(d);
                if (r.mirrorDestChannel>0 && dch>0 && dch!=r.mirrorDestChannel) {
                    // skip
                } else {
                    int rec = recorderForChild(d);
                    // Do not mirror master (slot 0) to recorder
                    if (rec>=0 && s != 0) desired.insert(edgeKey(s,rec), Edge{s,rec,r.level});
                }
                }
            }
        }
    }

    // Also add announcement player edges: player (source) -> each child slot of the parent
    for (auto it = m_parentToPlayerSlot.constBegin(); it != m_parentToPlayerSlot.constEnd(); ++it) {
        const QString &parentKey = it.key();
        int pslot = it.value();
        if (pslot == PJSUA_INVALID_ID) continue;
        const QList<int> children = getChildSlotsForParent(parentKey);
        for (int child : children) {
            if (child < 0) continue;
            // Default announcement level -3 dB (matches existing behavior)
            desired.insert(edgeKey(pslot, child), Edge{pslot, child, -3});
            s_audioRoutes er; er.srcSlot=pslot; er.destSlot=child; er.level=-3; er.persistant=false;
            m_ephemeralRoutes.append(er);
        }
    }

    // Always connect account's left call channel to recorder at 0 dB when recorder active
    for (auto it = m_parentToRecorderSlot.constBegin(); it != m_parentToRecorderSlot.constEnd(); ++it) {
        const QString &parentKey = it.key();
        // Defer recorder edges while announcement player is active for this parent
        if (m_parentToPlayerSlot.contains(parentKey)) continue;
        int rslot = it.value();
        if (rslot == PJSUA_INVALID_ID) continue;
        const QList<int> children = getChildSlotsForParent(parentKey);
        for (int child : children) {
            if (child < 0) continue;
            int ch = getChannelIndexForSlot(child);
            if (ch == 1) {
                desired.insert(edgeKey(child, rslot), Edge{child, rslot, 0});
                s_audioRoutes er; er.srcSlot=child; er.destSlot=rslot; er.level=0; er.persistant=false;
                m_ephemeralRoutes.append(er);
            }
        }
    }

    // For accounts configured as TX+RX recording: mirror all incoming sources to the recorder, not just left channel
    if (m_lib && m_lib->m_Accounts) {
        const QList<s_account>* accs = m_lib->m_Accounts->getAccounts();
        for (const s_account &acc : *accs) {
            if (acc.FileRecordRXonly) continue;
            const QString pkey = QString("ACC:%1").arg(acc.uid);
            // Defer recorder edges while announcement player is active for this parent
            if (m_parentToPlayerSlot.contains(pkey)) continue;
            int rslot = getRecorderForParent(pkey);
            if (rslot == PJSUA_INVALID_ID) continue;
            const QList<int> children = getChildSlotsForParent(pkey);
            // For each desired edge ending at any child of this account, mirror source to recorder
            for (auto it = desired.constBegin(); it != desired.constEnd(); ++it) {
                const Edge &e = it.value();
                if (children.contains(e.dst)) {
                    // Only mirror when destination child is left channel (Ch:1)
                    int dch = getChannelIndexForSlot(e.dst);
                    if (dch != 1) continue;
                    // Do not mirror master (slot 0) to recorder
                    if (e.src == 0) continue;
                    desired.insert(edgeKey(e.src, rslot), Edge{e.src, rslot, e.level});
                    s_audioRoutes er; er.srcSlot=e.src; er.destSlot=rslot; er.level=e.level; er.persistant=false;
                    m_ephemeralRoutes.append(er);
                }
            }
        }
    }

    // Fetch actual edges
    unsigned info_count = PJSUA_MAX_CONF_PORTS;
    QVector<pjmedia_conf_port_info> infos((int)info_count);
    pj_status_t st = pjmedia_conf_get_ports_info(intData->mconf, &info_count, infos.data());
    if (st != PJ_SUCCESS) return false;
    QHash<QString,int> actualLevel; // dB approximation
    QSet<QString> actualSet;

    auto adjToDb = [](int adj)->int {
        if (adj <= -128) return -96;
        double gain = (double)(adj + 128) / 128.0;
        if (gain < 0.001) return -96;
        int db = (int)std::lround(20.0 * std::log10(gain));
        if (db < -36) db = -36;
        if (db > 26) db = 26;
        return db;
    };

    for (unsigned i = 0; i < info_count; ++i) {
        const pjmedia_conf_port_info &pi = infos[(int)i];
        int s = (int)pi.slot;
        if (s < 0) continue;
        unsigned lc = pi.listener_cnt;
        for (unsigned j = 0; j < lc; ++j) {
            int d = pi.listener_slots ? (int)pi.listener_slots[j] : PJSUA_INVALID_ID;
            if (d == PJSUA_INVALID_ID) continue;
            QString k = edgeKey(s,d);
            actualSet.insert(k);
            int adj = 0;
            if (pi.listener_adj_level) adj = (int)pi.listener_adj_level[j] - 128;
            actualLevel.insert(k, adjToDb(adj));
        }
    }

    // Apply connects for missing desired edges (with extra presence check to avoid races)
    for (auto it = desired.constBegin(); it != desired.constEnd(); ++it) {
        const QString &k = it.key(); const Edge &e = it.value();
        if (!actualSet.contains(k)) {
            int leveladjust = dBtoAdjLevel(const_cast<int&>(e.level));
            bool stillMissing = true;
            pjmedia_conf_port_info cpi_check;
            if (pjmedia_conf_get_port_info(intData->mconf, e.src, &cpi_check) == PJ_SUCCESS) {
                if (cpi_check.listener_slots && cpi_check.listener_cnt > 0) {
                    for (unsigned j=0;j<cpi_check.listener_cnt;++j) if ((int)cpi_check.listener_slots[j]==e.dst) { stillMissing=false; break; }
                }
            }
            if (stillMissing) pjmedia_conf_connect_port(intData->mconf, e.src, e.dst, leveladjust);
            const QString sName = srcMap.value(e.src);
            const QString dName = dstMap.value(e.dst);
            m_lib->m_Log->writeLog(4, QString("Reconcile: connect %1 -> %2 @ %3 dB [%4 -> %5]")
                                       .arg(e.src).arg(e.dst).arg(e.level)
                                       .arg(sName.isEmpty()? "?" : sName)
                                       .arg(dName.isEmpty()? "?" : dName));
            changed = true;
        } else {
            // Level correction if significantly different (>= 1 dB)
            int have = actualLevel.value(k, e.level);
            if (have != e.level) {
                int leveladjust = dBtoAdjLevel(const_cast<int&>(e.level));
                pjmedia_conf_adjust_conn_level(intData->mconf, e.src, e.dst, leveladjust);
                const QString sName = srcMap.value(e.src);
                const QString dName = dstMap.value(e.dst);
                m_lib->m_Log->writeLog(4, QString("Reconcile: adjust %1 -> %2 to %3 dB (was %4) [%5 -> %6]")
                                           .arg(e.src).arg(e.dst).arg(e.level).arg(have)
                                           .arg(sName.isEmpty()? "?" : sName)
                                           .arg(dName.isEmpty()? "?" : dName));
            changed = true;
            }
        }
    }

    // Apply disconnects for actual edges not desired (with presence check to avoid spam)
    for (auto it = actualSet.constBegin(); it != actualSet.constEnd(); ++it) {
        const QString &k = *it;
        if (!desired.contains(k)) {
            // parse key
            int sep = k.indexOf('>');
            int s = k.left(sep).toInt();
            int d = k.mid(sep+1).toInt();
            // Preserve master<->sounddevice keepalive connections used to keep hardware open
            if (s == 0) {
                QString dName = dstMap.value(d);
                if (dName.startsWith("AD:")) {
                    continue; // don't disconnect the master feed to AD channels
                }
            }
            if (d == 0) {
                QString sName = srcMap.value(s);
                if (sName.startsWith("AD:")) {
                    continue; // don't disconnect the AD channels feeding master (RX keepalive)
                }
            }
            bool exists = false;
            pjmedia_conf_port_info cpi_check;
            if (pjmedia_conf_get_port_info(intData->mconf, s, &cpi_check) == PJ_SUCCESS) {
                if (cpi_check.listener_slots && cpi_check.listener_cnt > 0) {
                    for (unsigned j=0;j<cpi_check.listener_cnt;++j) if ((int)cpi_check.listener_slots[j]==d) { exists=true; break; }
                }
            }
            if (exists) pjmedia_conf_disconnect_port(intData->mconf, s, d);
            const QString sName = srcMap.value(s);
            const QString dName = dstMap.value(d);
            m_lib->m_Log->writeLog(4, QString("Reconcile: disconnect %1 -> %2 [%3 -> %4]")
                                       .arg(s).arg(d)
                                       .arg(sName.isEmpty()? "?" : sName)
                                       .arg(dName.isEmpty()? "?" : dName));
            changed = true;
        }
    }

    return changed;
}

void AudioRouter::refreshConfPortMaps()
{
    m_confPortList = listConfPorts();
}

void AudioRouter::reconcileActiveRoutesNow()
{
    // Always emit after reconciliation so UI updates even when no pjmedia changes occur
    reconcileActiveConnections();
    emitRoutesChanged();
}

int AudioRouter::getMirroredLevelForChild(int childSlot, const QString &parentKey,
                                 int requiredDestChannel, int defaultLevel) const
{
    if (parentKey.isEmpty()) return defaultLevel;
    // Determine child's channel (used to verify source channel constraints)
    int childCh = 0; 
    {
        pjsua_conf_port_info inf; 
        if (pjsua_conf_get_port_info(childSlot, &inf) == PJ_SUCCESS) {
            QString nm = pj2Str(inf.name);
            int idx = nm.indexOf("Ch:");
            if (idx >= 0) {
                bool ok=false; 
                int val = nm.mid(idx+3).toInt(&ok); 
                if (ok) childCh = val;
            }
        }
    }
    const s_audioRoutes *fallback = nullptr;
    for (const s_audioRoutes &r : m_audioRoutes) {
        if (!r.destIsParent || r.destParentKey != parentKey) continue;
        if (requiredDestChannel > 0 && r.destParentChannel > 0 && r.destParentChannel != requiredDestChannel)
            continue;
        if (r.srcParentChannel > 0 && childCh > 0 && r.srcParentChannel != childCh)
            continue;
        if (r.destParentChannel == requiredDestChannel || (requiredDestChannel==0 && r.destParentChannel==childCh))
            return r.level;
        if (r.destParentChannel == 0) fallback = &r;
    }
    return fallback ? fallback->level : defaultLevel;
}

void AudioRouter::scheduleConferenceRefresh(int delayMs)
{
    // Ensure QTimer is started on this object's thread; coalesce calls
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(
            this,
            [this, delayMs]() { this->scheduleConferenceRefresh(delayMs); },
            Qt::QueuedConnection);
        return;
    }
    if (delayMs < 0) delayMs = 0;
    m_confRefreshDebounceTimer->start(delayMs);
}

void AudioRouter::scheduleRouteEvaluation(int delayMs)
{
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(
            this,
            [this, delayMs]() { this->scheduleRouteEvaluation(delayMs); },
            Qt::QueuedConnection);
        return;
    }
    if (delayMs < 0) delayMs = 0;
    m_routeEvalDebounceTimer->start(delayMs);
}

void AudioRouter::removeAllRoutesFromSlot(int slot)
{
    // First, aggressively sever any unknown connections at the conference level
    pjsua_data* intData = pjsua_get_var();
    if (intData && intData->mconf) {
        pj_status_t st;
        st = pjmedia_conf_disconnect_port_from_sources(intData->mconf, slot);
        if (st != PJ_SUCCESS) {
            char buf[50];
            pj_strerror(st, buf, sizeof(buf));
            m_lib->m_Log->writeLog(2, QString("removeAllRoutesFromSlot: disconnect from sources failed for slot %1: ").arg(slot) + buf);
        }
        st = pjmedia_conf_disconnect_port_from_sinks(intData->mconf, slot);
        if (st != PJ_SUCCESS) {
            char buf[50];
            pj_strerror(st, buf, sizeof(buf));
            m_lib->m_Log->writeLog(2, QString("removeAllRoutesFromSlot: disconnect from sinks failed for slot %1: ").arg(slot) + buf);
        }
    }

    // Then, purge any known routes in our model that involve this slot
    bool save = false;
    QMutableListIterator<s_audioRoutes> i(m_audioRoutes);
    while (i.hasNext()) {
        s_audioRoutes &route = i.next();
        if (route.srcSlot == slot || route.destSlot == slot) {
            if (route.persistant) save = true;
            i.remove();
        }
    }
    if (save) m_lib->m_Settings->saveAudioRoutes();
    emitRoutesChanged();
}

void AudioRouter::registerParentForSlot(int slot, AudioEntityType parentType, const QString &parentKey)
{
    if (slot == PJSUA_INVALID_ID || parentKey.isEmpty()) return;
    registerParentForSlot(slot, parentType, parentKey, 0);
}

void AudioRouter::registerParentForSlot(int slot, AudioEntityType parentType, const QString &parentKey, int channelIndex)
{
    if (slot == PJSUA_INVALID_ID || parentKey.isEmpty()) return;
    // Remove from previous parent if re-parenting
    if (m_slotToParentKey.contains(slot)) {
        QString oldKey = m_slotToParentKey.value(slot);
        if (m_parentKeyToChildSlots.contains(oldKey)) {
            m_parentKeyToChildSlots[oldKey].removeAll(slot);
        }
    }
    m_slotToParentKey[slot] = parentKey;
    m_slotToParentType[slot] = parentType;
    if (channelIndex >= 0) m_slotChannelIndex[slot] = channelIndex;
    if (!m_parentKeyToChildSlots.contains(parentKey)) m_parentKeyToChildSlots[parentKey] = {};
    if (!m_parentKeyToChildSlots[parentKey].contains(slot)) m_parentKeyToChildSlots[parentKey].append(slot);

    // Refresh current maps so name lookups succeed immediately
    m_confPortList = listConfPorts();
    // Apply any stored parent routes now that the child has a real slot
    applyParentRoutesToChildSlot(slot, parentType, parentKey);
    // If this slot is a SIP call and stereo is expected, trigger a second pass to catch the sibling channel after PJSIP completes attachments
    scheduleRouteEvaluation(200);
    // Update UI
    scheduleConferenceRefresh(150);
}

void AudioRouter::unregisterSlot(int slot)
{
    // Remove any routes that involve this child slot (ephemeral or otherwise)
    removeAllRoutesFromSlot(slot);
    if (m_slotToParentKey.contains(slot)) {
        QString key = m_slotToParentKey.value(slot);
        if (m_parentKeyToChildSlots.contains(key)) m_parentKeyToChildSlots[key].removeAll(slot);
        m_slotToParentKey.remove(slot);
    }
    if (m_slotToParentType.contains(slot)) m_slotToParentType.remove(slot);
}

void AudioRouter::attachStreamChannels(const QString &parentKey,
    const QString &sessionKey,
    const QString &accountName,
    const QString &remoteNumber,
    pjmedia_port *streamPort,
    int channelCount)
{
    if (parentKey.isEmpty() || sessionKey.isEmpty() || !streamPort) return;
    // Create per-session pool
    if (!m_sessionToPool.contains(sessionKey)) {
        QString poolName = QString("SIP-%1").arg(sessionKey);
        pj_pool_t *pool = pj_pool_create(&pjsua_get_var()->cp.factory, poolName.toStdString().c_str(), 4096, 4096, NULL);
        m_sessionToPool.insert(sessionKey, pool);
    }
    pj_pool_t *pool = m_sessionToPool.value(sessionKey);

    // Determine channel count
    int chcnt = channelCount;
    if (chcnt <= 0) chcnt = 1;

    QList<int> confSlots;
    QList<pjmedia_port*> revPorts;
    pjmedia_master_port *master = nullptr;
    pjmedia_port *split = nullptr;

    if (chcnt <= 1) {
        // Mono: add streamPort directly with unified labeling
        QString name;
        if (parentKey.startsWith("WRTC_CH:"))
            name = QString("WRTC:%1-%2-Ch:1").arg(accountName, remoteNumber);
        else if (parentKey.startsWith("LM:"))
            name = QString("LM:%1-Ch:1").arg(accountName);
        else
            name = QString("SIP:%1-%2-Ch:1").arg(accountName, remoteNumber);
        pj_strdup2(pool ? pool : m_lib->pool, &streamPort->info.name, name.toStdString().c_str());
        pjsua_conf_port_id slot;
        if (pjsua_conf_add_port(pool ? pool : m_lib->pool, streamPort, &slot)==PJ_SUCCESS) {
            confSlots.append(slot);
            registerParentForSlot(slot, parentKey.startsWith("LM:") ? Entity_AudioDevice : Entity_SipAccount, parentKey, 1);
        }
    } else {
        // Stereo+: create splitcomb and master
        const pjmedia_port_info &pi = streamPort->info;
        int srate = PJMEDIA_PIA_SRATE(&pi);
        int spf = PJMEDIA_PIA_SPF(&pi);
        if (srate <= 0) srate = 48000;
        if (spf <= 0) spf = (srate * 20 * 2)/1000;
        if (pjmedia_splitcomb_create(pool ? pool : m_lib->pool, srate, 2, spf, 16, 0, &split) == PJ_SUCCESS && split) {
            if (pjmedia_master_port_create(pool ? pool : m_lib->pool, streamPort, split, 0, &master) == PJ_SUCCESS && master) {
                pjmedia_master_port_start(master);
                pjsua_conf_port_info masterInfo; pjsua_conf_get_port_info(0, &masterInfo);
                for (int ch=0; ch<2; ++ch) {
                    pjmedia_port *revch=nullptr;
                    if (pjmedia_splitcomb_create_rev_channel(pool ? pool : m_lib->pool, split, ch, 32, &revch) != PJ_SUCCESS || !revch) continue;
                    QString name;
                    if (parentKey.startsWith("WRTC_CH:"))
                        name = QString("WRTC:%1-%2-Ch:%3").arg(accountName, remoteNumber).arg(ch+1);
                    else if (parentKey.startsWith("LM:"))
                        name = QString("LM:%1-Ch:%2").arg(accountName).arg(ch+1);
                    else
                        name = QString("SIP:%1-%2-Ch:%3").arg(accountName, remoteNumber).arg(ch+1);
                    pj_strdup2(pool ? pool : m_lib->pool, &revch->info.name, name.toStdString().c_str());
                    pjsua_conf_port_id mslot=PJSUA_INVALID_ID;
                    if (pjsua_conf_add_port(pool ? pool : m_lib->pool, revch, &mslot)==PJ_SUCCESS) {
                        revPorts.append(revch);
                        confSlots.append(mslot);
                        // Use our API to create non-persistent keepalive routes for per-stream channels
                        connectConfPort(masterInfo.slot_id, mslot, -96, false);
                        connectConfPort(mslot, masterInfo.slot_id, -96, false);
                        registerParentForSlot(mslot, parentKey.startsWith("LM:") ? Entity_AudioDevice : Entity_SipAccount, parentKey, ch+1);
                    }
                }
            }
        }
    }

    if (!confSlots.isEmpty()) m_sessionToConfSlots.insert(sessionKey, confSlots);
    if (!revPorts.isEmpty()) m_sessionToRevPorts.insert(sessionKey, revPorts);
    if (master) m_sessionToMaster.insert(sessionKey, master);
    if (split) m_sessionToSplitcomb.insert(sessionKey, split);

    // Make sure maps/UI are fresh
    scheduleConferenceRefresh(150);
    scheduleRouteEvaluation(200);
}

void AudioRouter::detachParentStream(const QString &sessionKey)
{
    pj_pool_t *pool = m_sessionToPool.value(sessionKey, nullptr);
    // Remove routes and unregister slots
    for (int slot : m_sessionToConfSlots.value(sessionKey)) {
        removeAllRoutesFromSlot(slot);
        unregisterSlot(slot);
        pjsua_conf_remove_port(slot);
    }
    m_sessionToConfSlots.remove(sessionKey);
    // Stop and destroy master
    if (pjmedia_master_port *mp = m_sessionToMaster.value(sessionKey, nullptr)) {
        pjmedia_master_port_stop(mp);
        pjmedia_master_port_destroy(mp, PJ_FALSE);
        m_sessionToMaster.remove(sessionKey);
    }
    // Splitcomb is pool-owned
    m_sessionToSplitcomb.remove(sessionKey);
    m_sessionToRevPorts.remove(sessionKey);
    if (pool) { pj_pool_release(pool); m_sessionToPool.remove(sessionKey); }
    scheduleConferenceRefresh(150);
}

void AudioRouter::applyParentRoutesToChildSlot(int slot, AudioEntityType parentType, const QString &parentKey)
{
    Q_UNUSED(parentType);
    // Resolve slot name maps; listConfPorts must be fresh enough
    const QMap<int, QString> srcMap = getSrcAudioSlotMap();
    const QMap<int, QString> dstMap = getDestAudioSlotMap();
    const QString childNameAsSrc = srcMap.value(slot);
    const QString childNameAsDst = dstMap.value(slot);
    int childChIdxSrc = getChannelIndexForSlot(slot);
    int childChIdxDst = childChIdxSrc; // same slot used both sides depending on role
    if (childNameAsSrc.isEmpty() && childNameAsDst.isEmpty()) return;

    // Iterate unified routes and apply those involving this parent
    for (const s_audioRoutes &r : m_audioRoutes) {
        // Case A: child is source side of a parent route
        if (r.srcIsParent && r.srcParentKey == parentKey) {
            // Channel filter: if set, only apply when virtual channel matches. We use simple suffix check "Ch:X"
            if (r.srcParentChannel > 0 && childChIdxSrc > 0 && r.srcParentChannel != childChIdxSrc) {
                    continue;
            }
            if (r.destIsParent && !r.destParentKey.isEmpty()) {
                // Parent-to-parent: connect this child to all current children of dest parent
                const QList<int> destChildren = m_parentKeyToChildSlots.value(r.destParentKey);
                for (int sinkSlot : destChildren) {
                    // Filter destination children by destParentChannel if specified (allow cross-channel if r.destParentChannel==0 or != r.srcParentChannel)
                    if (r.destParentChannel > 0) {
                        int sinkCh = getChannelIndexForSlot(sinkSlot);
                        if (sinkCh > 0 && r.destParentChannel != sinkCh) continue;
                    }
                    // Reconcile will connect
                }
            } else if (!r.destDevName.isEmpty()) {
                int sinkSlot = dstMap.key(r.destDevName, -1);
                if (sinkSlot >= 0) {
                    // Reconcile will connect
                }
            }
        }
        // Case B: child is destination side of a parent route
        if (r.destIsParent && r.destParentKey == parentKey) {
            if (r.destParentChannel > 0 && childChIdxDst > 0 && r.destParentChannel != childChIdxDst) {
                    continue;
            }
            if (r.srcIsParent && !r.srcParentKey.isEmpty()) {
                const QList<int> srcChildren = m_parentKeyToChildSlots.value(r.srcParentKey);
                for (int srcSlot : srcChildren) {
                    // Filter source children by srcParentChannel if specified (allow cross-channel mapping when destParentChannel differs)
                    if (r.srcParentChannel > 0) {
                        int srcCh = getChannelIndexForSlot(srcSlot);
                        if (srcCh > 0 && r.srcParentChannel != srcCh) continue;
                    }
                    // Reconcile will connect
                }
            } else if (!r.srcDevName.isEmpty()) {
                int srcSlot = srcMap.key(r.srcDevName, -1);
                if (srcSlot >= 0) {
                    // Reconcile will connect
                }
            }
        }
    }
}

int AudioRouter::connectParentToDev(const QString &parentKey, AudioEntityType parentType, bool parentAsSource,
                           const QString &otherParentOrDev, int level, bool persistant,
                           int channelIndex, int otherChannelIndex)
{
    Q_UNUSED(parentType);
    // Create a unified parent route entry in m_audioRoutes
    s_audioRoutes r;
    r.level = level; r.persistant = persistant;
    if (parentAsSource) { r.srcIsParent = true; r.srcParentKey = parentKey; }
    else { r.destIsParent = true; r.destParentKey = parentKey; }
    // If other side is a parent key, detect prefix; else treat as devName
    if (otherParentOrDev.startsWith("ACC:") || otherParentOrDev.startsWith("WRTC_CH:")) {
        if (parentAsSource) { r.destIsParent = true; r.destParentKey = otherParentOrDev; }
        else { r.srcIsParent = true; r.srcParentKey = otherParentOrDev; }
    } else {
        if (parentAsSource) r.destDevName = otherParentOrDev; else r.srcDevName = otherParentOrDev;
    }
    // Apply provided channel indices (0 means any)
    if (parentAsSource) r.srcParentChannel = channelIndex; else r.destParentChannel = channelIndex;
    if (r.destIsParent && otherChannelIndex > 0) r.destParentChannel = otherChannelIndex;
    // Populate slot fields for serialization
    if (r.srcIsParent) {
        int ch = r.srcParentChannel > 0 ? r.srcParentChannel : 1;
        AudioEntityType t = parentType;
        if (t == Entity_Unknown) {
            if (r.srcParentKey.startsWith("ACC:")) t = Entity_SipAccount;
            else if (r.srcParentKey.startsWith("WRTC_CH:")) t = Entity_WebRTCChannel;
        }
        r.srcSlot = getOrCreateVirtualSlot(r.srcParentKey, t, true, ch);
    }
    if (r.destIsParent) {
        int ch = r.destParentChannel > 0 ? r.destParentChannel : 1;
        AudioEntityType t = Entity_Unknown;
        if (r.destParentKey.startsWith("ACC:")) t = Entity_SipAccount;
        else if (r.destParentKey.startsWith("WRTC_CH:")) t = Entity_WebRTCChannel;
        r.destSlot = getOrCreateVirtualSlot(r.destParentKey, t, false, ch);
    }
    if (!r.destIsParent && !r.destDevName.isEmpty()) {
        r.destSlot = m_destAudioSlotMap.key(r.destDevName, PJSUA_INVALID_ID);
    }
    if (!r.srcIsParent && !r.srcDevName.isEmpty()) {
        r.srcSlot = m_srcAudioSlotMap.key(r.srcDevName, PJSUA_INVALID_ID);
    }
    m_audioRoutes.append(r);
    m_lib->m_Settings->saveAudioRoutes();
    emitRoutesChanged();

    // Try immediate application for existing children
    int applied = PJ_SUCCESS;
    const QList<int> children = m_parentKeyToChildSlots.value(parentKey);
    for (int childSlot : children) {
        applyParentRoutesToChildSlot(childSlot, parentType, parentKey);
    }
    scheduleConferenceRefresh(100);
    return applied;
}

void AudioRouter::removeAllRoutesFromAccount(const s_account account)
{
    int slot;
    for (unsigned int i = 0; i< m_lib->epCfg.medConfig.channelCount; i++){
        slot = m_srcAudioSlotMap.key(QString("Acc:" + account.uid + "-Ch:" + QString::number(i+1)), -1);
        if(slot != -1){
            removeAllRoutesFromSlot(slot);
        }else{
            m_lib->m_Log->writeLog(2,(QString("Slot for Account ") + account.name + " Ch: " + QString::number(i+1) + " not found! No Routes could be removed!"));
        }
    }
}

void AudioRouter::removeAllRoutesForParentKey(const QString &parentKey)
{
    if (parentKey.isEmpty()) return;
    bool save = false; bool changed = false;
    // Remove parent-intent routes where this key appears on either side
    QMutableListIterator<s_audioRoutes> it(m_audioRoutes);
    while (it.hasNext()) {
        const s_audioRoutes &r = it.next();
        if ((r.srcIsParent && r.srcParentKey == parentKey) || (r.destIsParent && r.destParentKey == parentKey)) {
            save = save || r.persistant;
            it.remove();
            changed = true;
        }
    }
    // Remove child-ephemeral routes that belong to this parent's children
    const QList<int> children = m_parentKeyToChildSlots.value(parentKey);
    if (!children.isEmpty()) {
        QMutableListIterator<s_audioRoutes> di(m_audioRoutes);
        while (di.hasNext()) {
            const s_audioRoutes &dr = di.next();
            if (!dr.srcIsParent && !dr.destIsParent && dr.persistant == false && (children.contains(dr.srcSlot) || children.contains(dr.destSlot))) {
                di.remove();
                changed = true;
            }
        }
    }
    if (save) m_lib->m_Settings->saveAudioRoutes();
    if (changed) emitRoutesChanged();
}

void AudioRouter::changeConfportsrcName(const QString portName, const QString customName)
{
    if(customName.isEmpty()){
        m_customSourceLabels[portName].clear();
        scheduleConferenceRefresh(150);
        return;
    }
    m_customSourceLabels[portName] = customName;
    scheduleConferenceRefresh(150);
    m_lib->m_Settings->saveCustomSourceNames();
}

void AudioRouter::changeConfportdstName(const QString portName, const QString customName)
{
    if(customName.isEmpty()){
        m_customDestLabels[portName].clear();
        scheduleConferenceRefresh(150);
        return;
    }
    m_customDestLabels[portName] = customName;
    scheduleConferenceRefresh(150);
    m_lib->m_Settings->saveCustomDestinationNames();
}

void AudioRouter::removeAllCustomNamesWithUID(const QString uid)
{
    QMap<QString, QString>::const_iterator it = m_customSourceLabels.constBegin();
    auto end = m_customSourceLabels.constEnd();
    while (it != end) {
        if(it.key().contains(uid)){
            m_customSourceLabels.remove(it.key());
        }
        ++it;
    }
    it = m_customDestLabels.constBegin();
    end = m_customDestLabels.constEnd();
    while (it != end) {
        if(it.key().contains(uid)){
            m_customDestLabels.remove(it.key());
        }
        ++it;
    }
    m_lib->m_Settings->saveCustomSourceNames();
    m_lib->m_Settings->saveCustomDestinationNames();
}


void AudioRouter::SoundDeviceInspector()
{
    pjmedia_aud_dev_refresh() ;
    uint8_t count = pjmedia_snd_get_dev_count();
    int i;
    if(count == m_sounddevCount){                                       // if the number of audio devices in the system did not change we assume that nothing changed
        return;
    }

    if(count > m_sounddevCount){                                        // if the number of audio devices changed: check if devices marked as offline are still offline
        for (auto &audiodev : m_AudioDevices){
            if(audiodev.devicetype == SoundDevice && audiodev.PBDevID ==-1 && audiodev.RecDevID ==-1){
                for (i=0; i<count; ++i)
                {
                    const pjmedia_snd_dev_info *info;
                    info = pjmedia_snd_get_dev_info(i);
                    if(info->name == audiodev.inputname){
                        int recDevId = getSoundDevID(audiodev.inputname);
                        int pbDevId = getSoundDevID(audiodev.outputame);
                        m_lib->m_Log->writeLog(3,QString("SoundDeviceInspector: offline sound device: ") + audiodev.inputname + " is now avaliable ");
                        addAudioDevice(recDevId,pbDevId,audiodev.uid);
                        const QMap<int, QString> srcAudioSlotMap = getSrcAudioSlotMap();
                        const QMap<int, QString> destAudioSlotMap = getDestAudioSlotMap();
                        QMutableListIterator<s_audioRoutes> i(m_offlineRoutes);
                        while(i.hasNext()){
                            s_audioRoutes& route = i.next();
                            route.srcSlot = srcAudioSlotMap.key(route.srcDevName, -1);
                            route.destSlot = destAudioSlotMap.key(route.destDevName, -1);
                            int check = m_lib->m_AudioRouter->connectConfPort(route.srcSlot, route.destSlot, route.level, route.persistant);
                            if(check == PJ_SUCCESS){
                                m_lib->m_Log->writeLog(3,QString("SoundDeviceInspector: added AudioRoute from: ") + route.srcDevName + " to " + route.destDevName);
                                i.remove();
                            }
                        }
                    }
                }
            }
        }
    }

    if(count < m_sounddevCount){                                        // if the number of audio devices changed: check also if existing devices are still online
        for (auto &audiodev : m_AudioDevices){
            if(audiodev.devicetype == SoundDevice ){
                bool devicefound = false;
                for (i=0; i<count; ++i)
                {
                    const pjmedia_snd_dev_info *info;
                    info = pjmedia_snd_get_dev_info(i);
                    if(info->name == audiodev.inputname){
                        devicefound = true;
                        break;
                    }
                }
                if(devicefound == false){
                    m_lib->m_Log->writeLog(3,QString("SoundDeviceInspector: existing sound device: ") + audiodev.inputname + " lost!");
                    setAudioDeviceToOffline(audiodev.inputname,audiodev.outputame, audiodev.uid);
                }
            }
        }
    }
    m_sounddevCount = count;
}

void AudioRouter::emitRoutesChanged()
{
    // Merge persistent and ephemeral routes for UI emission
    QList<s_audioRoutes> merged = m_audioRoutes;
    for (const s_audioRoutes &er : m_ephemeralRoutes) {
        merged.append(er);
    }
    // Enrich with device names where missing so UI can display child routes
    for (int i = 0; i < merged.size(); ++i) {
        s_audioRoutes &r = merged[i];
        if (r.srcDevName.isEmpty() && r.srcSlot >= 0) {
            r.srcDevName = m_srcAudioSlotMap.value(r.srcSlot);
        }
        if (r.destDevName.isEmpty() && r.destSlot >= 0) {
            r.destDevName = m_destAudioSlotMap.value(r.destSlot);
        }
    }
    emit audioRoutesChanged(merged);
}

int AudioRouter::addAnnouncementPlayerForParent(const QString &parentKey, const QString &filePath, const QString &uid)
{
    if (parentKey.isEmpty() || filePath.isEmpty()) return PJSUA_INVALID_ID;
    pjsua_player_id player_id; pj_status_t status; pjmedia_port *player_media_port = nullptr;
    pj_str_t fname = pj_strdup3(m_lib->pool, filePath.toStdString().c_str());
    status = pjsua_player_create(&fname, PJMEDIA_FILE_NO_LOOP, &player_id);
    if (status != PJ_SUCCESS) {
        char buf[64]; pj_strerror(status, buf, sizeof(buf));
        m_lib->m_Log->writeLog(1, QString("addAnnouncementPlayerForParent: create failed: ") + buf);
        return PJSUA_INVALID_ID;
    }
    if (pjsua_player_get_port(player_id, &player_media_port) != PJ_SUCCESS) return PJSUA_INVALID_ID;
    // Set a clear label, keep PJSUA's auto conf slot so listConfPorts can discover it via player_id
    QString name = QString("FP:%1-Announcement").arg(uid.isEmpty()? createNewUID() : uid);
    pj_strdup2(m_lib->pool, &player_media_port->info.name, name.toStdString().c_str());
    int slot = pjsua_player_get_conf_port(player_id);
    if (slot != PJSUA_INVALID_ID) {
        registerPlayerForParent(parentKey, slot);
        m_parentToPlayerId[parentKey] = player_id;
        scheduleRouteEvaluation(50);
        emitRoutesChanged();
    }
    struct EofCtx { AudioRouter *router; QString parentKey; };
    EofCtx *ctx = new EofCtx{ this, parentKey };
    pjmedia_wav_player_set_eof_cb2(player_media_port, ctx, [](pjmedia_port*, void *user_data){
        EofCtx *c = static_cast<EofCtx*>(user_data);
        if (!c || !c->router) { delete c; return; }
        c->router->onAnnouncementFinished(c->parentKey);
        delete c;
    });
    return player_id;
}

void AudioRouter::onAnnouncementFinished(const QString &parentKey)
{
	if (parentKey.isEmpty()) return;
	m_lib->m_Log->writeLog(3, QString("AudioRouter: announcement finished for %1").arg(parentKey));
	// Remove routes and destroy the announcement player deterministically
	int pslot = m_parentToPlayerSlot.value(parentKey, PJSUA_INVALID_ID);
	if (pslot != PJSUA_INVALID_ID) {
		removeAllRoutesFromSlot(pslot);
	}
	int pid = m_parentToPlayerId.value(parentKey, PJSUA_INVALID_ID);
	if (pid != PJSUA_INVALID_ID) {
		pjsua_player_destroy(pid);
		m_parentToPlayerId.remove(parentKey);
	}
	if (m_parentToPlayerSlot.contains(parentKey)) {
		m_parentToPlayerSlot.remove(parentKey);
	}
	scheduleConferenceRefresh(100);
	scheduleRouteEvaluation(50);
	emitRoutesChanged();
}

int AudioRouter::addCallRecorderForParent(const QString &parentKey, const QString &filePath, const QString &uid)
{
    Q_UNUSED(uid);
    if (parentKey.isEmpty() || filePath.isEmpty()) return PJSUA_INVALID_ID;
    pjsua_recorder_id rec_id; pj_status_t status; pjmedia_port *media_port = nullptr;
    pj_str_t rec_file = pj_strdup3(m_lib->pool, filePath.toStdString().c_str());
    status = pjsua_recorder_create(&rec_file, 0, NULL, 0, 0, &rec_id);
    if (status != PJ_SUCCESS) {
        char buf[64]; pj_strerror(status, buf, sizeof(buf));
        m_lib->m_Log->writeLog(1, QString("addCallRecorderForParent: Error creating recorder: ") + buf);
        return PJSUA_INVALID_ID;
    }
    if (pjsua_recorder_get_port(rec_id, &media_port) != PJ_SUCCESS) return PJSUA_INVALID_ID;
    int rslot = pjsua_recorder_get_conf_port(rec_id);
    if (rslot != PJSUA_INVALID_ID) registerRecorderForParent(parentKey, rslot, rec_id);
    scheduleRouteEvaluation(50);
    emitRoutesChanged();
    return rec_id;
}

void AudioRouter::teardownParentMedia(const QString &parentKey)
{
    if (parentKey.isEmpty()) return;
    // Player
    if (m_parentToPlayerSlot.contains(parentKey)) {
        int pslot = m_parentToPlayerSlot.value(parentKey, PJSUA_INVALID_ID);
        if (pslot != PJSUA_INVALID_ID) removeAllRoutesFromSlot(pslot);
        int pid = m_parentToPlayerId.value(parentKey, PJSUA_INVALID_ID);
        if (pid != PJSUA_INVALID_ID) {
            pjsua_player_destroy(pid);
            m_parentToPlayerId.remove(parentKey);
        }
        m_parentToPlayerSlot.remove(parentKey);
    }
    // Restore master keepalive routes for this parent's active children (if any)
    /* const QList<int> children = m_parentKeyToChildSlots.value(parentKey);
    for (int cslot : children) {
        if (cslot == PJSUA_INVALID_ID) continue;
        // Left/right child slots get master keepalive both directions
        connectConfPort(0, cslot, -96, false);
        connectConfPort(cslot, 0, -96, false);
    } */
    // Recorder
    if (m_parentToRecorderSlot.contains(parentKey)) {
        int rslot = m_parentToRecorderSlot.value(parentKey, PJSUA_INVALID_ID);
        if (rslot != PJSUA_INVALID_ID) removeAllRoutesFromSlot(rslot);
        int rid = m_parentToRecorderId.value(parentKey, PJSUA_INVALID_ID);
        if (rid != PJSUA_INVALID_ID) {
            pjsua_recorder_destroy(rid);
            m_parentToRecorderId.remove(parentKey);
        }
        m_parentToRecorderSlot.remove(parentKey);
    }
    scheduleRouteEvaluation(50);
    emitRoutesChanged();
}
