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

#include "audiorouter.h"
#include "awahsiplib.h"
#include "pjmedia.h"
#include "pjlib-util.h" /* pj_getopt */
#include "pjlib.h"
#include "pjsua-lib/pjsua_internal.h"
#include "pj/string.h"
#include <QDebug>
#include <QThread>
#include <QSettings>

#define THIS_FILE		"audiorouter.cpp"


AudioRouter::AudioRouter(AWAHSipLib *parentLib, QObject *parent) : QObject(parent), m_lib(parentLib)
{ 
    m_SoundDeviceInspectorTimer = new QTimer(this);
    m_SoundDeviceInspectorTimer->setInterval(5000);
    connect(m_SoundDeviceInspectorTimer, SIGNAL(timeout()), this, SLOT(SoundDeviceInspector()));
    m_sounddevCount = pjmedia_snd_get_dev_count();
    m_SoundDeviceInspectorTimer->start();
}

AudioRouter::~AudioRouter()
{
    m_SoundDeviceInspectorTimer->stop();
    QList<s_IODevices> *audioDevs = getAudioDevices();
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
    QString snddev;
    foreach(AudioDevInfo audiodev, m_lib->m_pjEp->audDevManager().enumDev2()){
        snddev.clear();
        if(audiodev.inputCount>0){
            snddev.append(QString::fromStdString(audiodev.name));
            snddevlist << snddev;
        }
    }
    return snddevlist;
}

QStringList AudioRouter::listOutputSoundDev(){
    pjmedia_aud_dev_refresh() ;
    QStringList snddevlist;
    QString snddev;
    foreach(AudioDevInfo audiodev, m_lib->m_pjEp->audDevManager().enumDev2()){
        snddev.clear();
        if(audiodev.outputCount>0){
            snddev.append(QString::fromStdString(audiodev.name));
            snddevlist << snddev;
        }
    }
    return snddevlist;
}


int AudioRouter::getSoundDevID(QString DeviceName)
{
    int id = 0;
    foreach(AudioDevInfo audiodev, m_lib->m_pjEp->audDevManager().enumDev2()){
        if (audiodev.name == DeviceName.toStdString()){
            return id;
        }
        id++;
    }
    return -1;
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

//    for(auto& device : m_AudioDevices){
//        if(device.devicetype == SoundDevice){
//            qDebug() << "Add audio device name: " << device.inputname;
//            qDebug() << "Add audio device info rec id: " << device.RecDevID;
//            qDebug() << "Add audio device info pb id: " << device.PBDevID;
//            qDebug() << "Add audio input rec id: " << recordDevId;
//            qDebug() << "Add audio input pb id: " << playbackDevId;
//            if(device.RecDevID >-1 && device.RecDevID >-1 && (device.RecDevID == recordDevId || device.PBDevID == playbackDevId)){
//                m_lib->m_Log->writeLog(3,"AddAudioDevice: error could not add device. Device already exists!" );
//                //return;
//            }
//        }
//    }

    recorddev =  m_lib->m_pjEp->audDevManager().getDevInfo(recordDevId);
    playbackdev = m_lib->m_pjEp->audDevManager().getDevInfo(playbackDevId);
    if(uid.isEmpty())
        uid = createNewUID();

    if(recorddev.inputCount >= playbackdev.outputCount){                            // if rec and playback device have different channelcount
        channelCnt =recorddev.inputCount;                                         // use the bigger number
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
        pjsua_conf_connect(masterPortInfo.slot_id,slot);        // connect masterport to sound dev to keep it open all the time to prevent different latencies (see issue #29)
        pjsua_conf_connect(slot,masterPortInfo.slot_id);

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
    m_lib->m_Settings->saveIODevConfig();
    conferenceBridgeChanged();
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
        emit AudioDevicesChanged(m_AudioDevices);
        return;
    }
    else{                                               // device was online an is lost. Some cleanup is needed
        if(offlineDevice->devicetype == SoundDevice)
        {
            if(offlineDevice->PBDevID > -1 && offlineDevice->RecDevID > -1){

                for(auto& slot : offlineDevice->portNo){
                    try{
                        pj_status_t status;
                        bool save = false;
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
                                route.persistant ? (save = true) : false;
                                if(save){
                                    m_offlineRoutes.append(route);
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
        conferenceBridgeChanged();
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
        m_lib->m_Log->writeLog(3,"removeAudioDevice: device not an audio device");
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
    conferenceBridgeChanged();
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
    conferenceBridgeChanged();
    emit AudioDevicesChanged(m_AudioDevices);
    return;
}


void AudioRouter::addFilePlayer(QString PlayerName, QString File, QString uid)
{
    pjsua_data* intData = pjsua_get_var();
    pjsua_player_id player_id;
    pj_status_t status;
    const pj_str_t sound_file = pj_strdup3 (m_lib->pool, File.toStdString().c_str());
    pjmedia_port *player_media_port;
    s_IODevices Audiodevice;
    int slot;

    if(uid.isEmpty())
        uid = createNewUID();
    QString name = "FP:" + uid + "-File Player " + PlayerName;

    status = pjsua_player_create(&sound_file, 0, &player_id);
    if (status != PJ_SUCCESS) {
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(1,(QString("Fileplayer: create player failed: ") + buf));
        return;
    }

    status = pjsua_player_get_port(player_id, &player_media_port);
    if (status != PJ_SUCCESS) {
        return;
    }
    pj_strdup2(m_lib->pool, &player_media_port->info.name, name.toStdString().c_str());
    status = pjsua_conf_add_port(m_lib->pool, player_media_port, &slot);
    if (status != PJ_SUCCESS) {
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(2,(QString("Fileplayer: connecting file player to conference bridge failed: ") + buf));
        return;
    }
    int level = -3;
    status = pjmedia_conf_adjust_rx_level(intData->mconf, slot, dBtoAdjLevel(level));
    if (status != PJ_SUCCESS) {
        return;
    }

    Audiodevice.devicetype = FilePlayer;                             // update devicelist for saving and recalling current setup
    Audiodevice.inputname = PlayerName;
    Audiodevice.uid = uid;
    Audiodevice.path = File;
    Audiodevice.portNo.append(slot);
    Audiodevice.PBDevID = player_id;
    Audiodevice.mediaport = player_media_port;
    m_AudioDevices.append(Audiodevice);
    conferenceBridgeChanged();
    m_lib->m_Settings->saveIODevConfig();
    emit AudioDevicesChanged(m_AudioDevices);
    return;
}


void AudioRouter::addFileRecorder(QString File, QString uid)
{
    pjsua_recorder_id rec_id;
    pj_status_t status;
    const pj_str_t sound_file = pj_strdup3 (m_lib->pool, File.toStdString().c_str());
    pjmedia_port *media_port;
    s_IODevices Audiodevice;
    int slot;
    if(uid.isEmpty())
        uid = createNewUID();
    QString name = "FR:" + uid + "-File Recorder " + File;

    status = pjsua_recorder_create(&sound_file, 0, 0, -1, 0,&rec_id);
    if (status != PJ_SUCCESS) {
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(1,(QString("Filerecorder: create failed: " ) + buf));
        return;
    }
    status = pjsua_recorder_get_port(rec_id, &media_port);
    if (status != PJ_SUCCESS)
    {
        return;
    }
    pj_strdup2(m_lib->pool, &media_port->info.name, name.toStdString().c_str());
    status = pjsua_conf_add_port(m_lib->pool, media_port, &slot);
    if (status != PJ_SUCCESS) {
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(2,(QString("FileRecorder: connecting file recorder to conference bridge failed: ") + buf));
        return;
    }

    Audiodevice.devicetype = FileRecorder;                             // update devicelist for saving and recalling current setup
    Audiodevice.outputame = name;
    Audiodevice.uid = uid;
    Audiodevice.path = File;
    Audiodevice.portNo.append(slot);
    Audiodevice.RecDevID = rec_id;
    Audiodevice.mediaport = media_port;
    m_AudioDevices.append(Audiodevice);
    m_lib->m_Settings->saveIODevConfig();
    conferenceBridgeChanged();
    emit AudioDevicesChanged(m_AudioDevices);
    return;
}

int AudioRouter::addSplittComb(s_account &account)
{
    pj_status_t status;
    pjsua_conf_port_info masterPortInfo;
    int channelCnt = m_lib->epCfg.medConfig.channelCount;
    int slot;
    status = pjsua_conf_get_port_info( 0, &masterPortInfo );                           // get the clockrate from master port
    if (status != PJ_SUCCESS) {
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(1,(QString("AddSplittComb: Error reading master port info: ") + buf));
        return -1;
    }

    status = pjmedia_splitcomb_create(
                /* pointer to the memory pool */        m_lib->pool,
                /* clock rate*/                         masterPortInfo.clock_rate,
                /*channel count */                      channelCnt,
                /*samples per frame*/                   2 * masterPortInfo.samples_per_frame,
                /* bits per sample*/                    masterPortInfo.bits_per_sample,
                /* options*/                            0,
                &account.splitComb);
    if (status != PJ_SUCCESS){
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(1,(QString("AddSplittComb:  could not create splittcomb: ") + buf));
        return -1;
    }

    for (int i = 0; i<channelCnt;i++)
    {
        pjmedia_port *revch;
        QString name = "Acc:" + account.uid + "-Ch:" + QString::number(i+1);
        status = pjmedia_splitcomb_create_rev_channel(m_lib->pool, account.splitComb, i, 0, &revch);
        if (status != PJ_SUCCESS){
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf) );
            m_lib->m_Log->writeLog(1,(QString("AddSplittComb:  could not create splittcomb revchannel: ") + buf));
            return -1;
        }
        pj_strdup2(m_lib->pool, &revch->info.name, name.toStdString().c_str());
        status = pjsua_conf_add_port(m_lib->pool, revch, &slot);
        if (status != PJ_SUCCESS){
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf) );
            m_lib->m_Log->writeLog(1,(QString("AddSplittComb: adding port failed: ") + buf));
            return -1;
        }
        pjsua_conf_connect(masterPortInfo.slot_id,slot);        // connect masterport to sound dev to keep it open all the time to prevent different latencies (see issue #29)
        pjsua_conf_connect(slot,masterPortInfo.slot_id);
    }

    status = pjsua_conf_add_port(m_lib->pool, account.splitComb, &slot);
    if (status != PJ_SUCCESS){
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(1,(QString("AddSplittComb: adding port failed: ") + buf));
        return -1;
    }
    pjsua_conf_connect(masterPortInfo.slot_id,slot);        // connect masterport to sound dev to keep it open all the time to prevent different latencies (see issue #29)
    pjsua_conf_connect(slot,masterPortInfo.slot_id);


    account.splitterSlot = slot;
    return PJ_SUCCESS;
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
                        audioPortList.destPorts.append(dest);
                        m_destAudioSlotMap[slot] = pj2Str(confinfo.name);
                    }
                }
            }
        } else if(portName.startsWith("Acc:")){
            QString uid = split[0].remove("Acc:");
            const s_account* account = m_lib->m_Accounts->getAccountByUID(uid);
            if(account != nullptr) {
                src.pjName = confinfo.name;
                if(m_customSourceLabels.contains(pj2Str(src.pjName))){
                    src.name = m_customSourceLabels[pj2Str(src.pjName)];
                }
                else{
                    src.name = account->name + " " + split.at(1);
                }
                src.slot = slot;
                dest.pjName = confinfo.name;
                if(m_customDestLabels.contains(pj2Str(dest.pjName))){
                    dest.name = m_customDestLabels[pj2Str(dest.pjName)];
                }
                else{
                    dest.name = account->name + " " + split.at(1);
                }
                dest.slot = slot;
                audioPortList.srcPorts.append(src);
                audioPortList.destPorts.append(dest);
                m_srcAudioSlotMap[slot] = pj2Str(confinfo.name);
                m_destAudioSlotMap[slot] = pj2Str(confinfo.name);
            }
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
                    audioPortList.destPorts.append(dest);
                    m_srcAudioSlotMap[slot] = pj2Str(confinfo.name);
                }
            }
        }
        debugSlotOut.clear();
        debugSlotOut.append(QString::number(slot));
        debugSlotOut.append(": ");
        debugSlotOut.append(confinfo.name.ptr);
        confportlist.append(debugSlotOut);
    }
    return audioPortList;
}

int AudioRouter::connectConfPort(int src_slot, int sink_slot, int level, bool persistant)
{
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
    pjsua_conf_connect_param param;
    param.level = dBtoFact(level);
    status = pjsua_conf_connect2(src, sink, &param);
    if (status == PJ_SUCCESS){
        m_lib->m_Log->writeLog(3,(QString("connect slot: ") + QString::number(src_slot) + " to " + QString::number(sink_slot) + " successfully" ));
        bool routeExists = false;
        for(auto& existingroute : m_audioRoutes){                                                   // check if route is already connected
            if(existingroute.srcSlot == route.srcSlot && existingroute.destSlot == route.destSlot){
                existingroute = route;
                routeExists = true;
                m_lib->m_Log->writeLog(2,(QString("connectConfPort: route already connected! updating it instead: ") + QString::number(src_slot) + " to " + QString::number(sink_slot)));
                break;
            }
        }
        if(!routeExists){
            m_audioRoutes.append(route);
        }
        emit audioRoutesChanged(m_audioRoutes);
        changeConfPortLevel(src_slot,sink_slot, level);     // this is called to set the exact db values with the factor used in this function it is not in every case correct!!
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
    pj_status_t status;
    pjsua_conf_port_id src = src_slot;
    pjsua_conf_port_id sink = sink_slot;
    status = pjsua_conf_disconnect(src, sink);
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
        emit audioRoutesChanged(m_audioRoutes);
    }
    else{
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(2,(QString("DisonnectConfPort: dissconect slot failed: ") + buf));
    }
    return status;
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
    emit audioRoutesTableChanged(m_confPortList);
    emit audioRoutesChanged(m_audioRoutes);
}

void AudioRouter::removeAllRoutesFromSlot(int slot)
{
    pj_status_t status;
    bool save = false;
    QMutableListIterator<s_audioRoutes> i(m_audioRoutes);
    while(i.hasNext()){
        s_audioRoutes& route = i.next();
        if(route.srcSlot == slot || route.destSlot == slot){
            status = pjsua_conf_disconnect(route.srcSlot, route.destSlot);
            if (status != PJ_SUCCESS){
                char buf[50];
                pj_strerror	(status,buf,sizeof (buf) );
                m_lib->m_Log->writeLog(2,(QString("RemoveAllRoutesFromSlot: disconnect slot failed from slot: ") + QString::number(route.srcSlot) + " : " + buf));
            }
            route.persistant ? (save = true) : false;
            i.remove();
        }
    }
    if(save)
        m_lib->m_Settings->saveAudioRoutes();
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

void AudioRouter::changeConfportsrcName(const QString portName, const QString customName)
{
    if(customName.isEmpty()){
        m_customSourceLabels[portName].clear();
        conferenceBridgeChanged();
        return;
    }
    m_customSourceLabels[portName] = customName;
    conferenceBridgeChanged();
    m_lib->m_Settings->saveCustomSourceNames();
}

void AudioRouter::changeConfportdstName(const QString portName, const QString customName)
{
    if(customName.isEmpty()){
        m_customDestLabels[portName].clear();
        conferenceBridgeChanged();
        return;
    }
    m_customDestLabels[portName] = customName;
    conferenceBridgeChanged();
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
