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

#ifndef AUDIOROUTER_H
#define AUDIOROUTER_H

#include <QObject>
#include <QMap>
#include "types.h"

class AWAHSipLib;

class AudioRouter : public QObject
{
    Q_OBJECT
public:
    explicit AudioRouter(AWAHSipLib *parentLib, QObject *parent = nullptr);
    ~AudioRouter();

    /**
    * @brief get the active audio routes
    * @return the audioRoutes struct
    */
    QList <s_audioRoutes> getAudioRoutes() { return m_audioRoutes; };

    /**
    * @brief List all input sound devicees available in the system
    * @return Stringlist wit the Names and channelcount. Position of the Name is the device ID
    */
    QStringList listInputSoundDev();

    /**
    * @brief List all output sound devicees available in the system
    * @return Stringlist wit the Names and channelcount. Position of the Name is the device ID
    */
    QStringList listOutputSoundDev();


    /**
    * @brief get the sound device id of the given sound device name
    * @return the device id or -1 if the device is not available anymore
    */
    int getSoundDevID(QString DeviceName);

    /**
    * @brief Add an Audio device to the conference bridge
    *        all channels of the device will be added (upto 64 channels)
    * @param recordDevId ID of the desired record device
    * @param playbackDevId ID of the dseired playback device
    * @param if the device is loaded from settings you have to provide it's uid to assign the correct routes
    * @return no of channels added or -1 on error
    */
    void addAudioDevice(int recordDevId, int playbackDevId, QString uid = "");

    /**
    * @brief Add an offline device to the the router
    *        it will not be added to the conference bridge but it will stay in the device config
    *        if you don't add it here it gets deleated
    * @param inputName  the Name of the Input device
    * @param outputName the name of the output devie
    * @param uid the uid of the device
    */
    void addOfflineAudioDevice(QString inputName, QString outputName, QString uid);


    /**
    * @brief remove a an audio device from the conference bridge.
    * @param the uid of the Device to be removed
    */
    void removeAudioDevice(QString uid);

    /**
    * @brief get an audio device by it's unique identifyer
    * @param uid  the unique identifyer
    * @return a pointer to the audio device struct
    */
    s_IODevices* getADeviceByUID(QString uid);

    /**
    * @brief add a file player to the conference bridge
    * @param Name displayed name of the player in the bridge
    * @param File path and filename of the audio file to be played
    * @param uid  the unique identifyer
    */
    void addFilePlayer(QString Name, QString File, QString uid = "");

    /**
    * @brief add a file recorder to the conference bridge
    * @param File the path                                              // todo richtig beschreiben wenn das
    * @param uid  the unique identifyer
    */
    void addFileRecorder(QString File, QString uid = "");

    /**
    * @brief Return the List of all active conference ports
    * @return Struct with names and Slot IDs for Sources and Destinations
    */
    const s_audioPortList& getConfPortsList() { return m_confPortList; }

    /**
    * @brief Make an unidirectional connection from a source slot to a sink
    * @param src_slot ID if the source slot
    * @param sink_slot ID of the sink_slot
    * @param presistant if set to ture the xp will be saved in the config file otherwise it is just a momentary connection
    * @param level The level in dB of the connetion -40 is the minimum, lower values mute the crosspoint, +20 is the maximum
    * @return PJ_SUCESS or the respective error code
    */
    int connectConfPort(int src_slot, int sink_slot, int level, bool persistant = true);

    /**
    * @brief Remove a connection between a source slot to a sink
    * @param src_slot ID if the source slot
    * @param sink_slot ID of the sink_slot
    * @return PJ_SUCESS or the respective error code
    */
    int disconnectConfPort(int src_slot, int sink_slot);

    /**
    * @brief Change Volume of an unidirectional connection from a source slot to a sink
    * @param src_slot ID if the source slot
    * @param sink_slot ID of the sink_slot
    * @param level The level in dB of the connetion -40 is the minimum, lower values mute the crosspoint, +20 is the maximum
    */
    void changeConfPortLevel(int src_slot, int sink_slot, int level);

    /**
    * @brief Add a sine wave generator
    * @param frequ Frequency in Hz
    */
    void addToneGen(int freq, QString uid = "");

    /**
    * @brief Add a Splitter-Combiner to the ConferenceBridge for an account
    * @param account account-struct to add the SplitterCombiner
    * @return PJ_SUCESS or the respective error code
    */
    int addSplittComb(s_account& account);

    /**
    * @brief Add a custom lable to a Confport source
    * @param portName the portName (defaultlable)
    * @param CustomName the new custom name for that source
    */
    void changeConfportsrcName(const QString portName, const QString customName);

    /**
    * @brief Add a custom lable to a Confport destination
    * @param portName the portName (defaultlable)
    * @param CustomName the new custom name for that destination
    */
    void changeConfportdstName(const QString portName, const QString customName);

    /**
    * @brief remove changed names with the uid (call this function when removing a audio device or an account to clean up the lables)
    * @param uid the uid of the account or the audio device
    */
    void removeAllCustomNamesWithUID(const QString uid);

    /**
    * @brief get the active devices
    * @return the AudioDevice struct
    */
    QList<s_IODevices>* getAudioDevices() { return &m_AudioDevices; };

    void conferenceBridgeChanged();
    void removeAllRoutesFromAccount(const s_account account);

    QMap<int, QString> getSrcAudioSlotMap() const { return m_srcAudioSlotMap; };
    QMap<int, QString> getDestAudioSlotMap() const { return m_destAudioSlotMap; };
    QMap<QString, QString> getCustomSourceLabels() const { return m_customSourceLabels; };
    QMap<QString, QString> getCustomDestLables() const { return m_customDestLabels; };
    void setCustomSourceLables(const QMap<QString, QString> srclables) { m_customSourceLabels = srclables; };
    void setCustomDestinationLables(const QMap<QString, QString> dstlables) { m_customDestLabels = dstlables; };

signals:

    /**
    * @brief Signal if audio device config changed
    * @param QList of the new device config
    */
    void AudioDevicesChanged(QList<s_IODevices>& audioDev);

    /**
    * @brief Signal if audio routes from the conference-bridge changed
    * @param audioRoutes all routes actually set as a QList
    */
    void audioRoutesChanged(QList<s_audioRoutes> audioRoutes);

    /**
    * @brief Signal if audio route Table from the conference-bridge changed
    * @param portList all Sources and Sinks as Struct
    */
    void audioRoutesTableChanged(const s_audioPortList& portList);

private:
    AWAHSipLib* m_lib;
    QMap<int, QString> m_srcAudioSlotMap;
    QMap<int, QString> m_destAudioSlotMap;
    s_audioPortList m_confPortList;

    /**
    * @brief All AudioDevices (soundcards, generators, fileplayer and recoder) are added to this list
    *       in order to save and load current audio setup
    */
    QList<s_IODevices> m_AudioDevices;

    /**
    * @brief All routes from the Conference Bridge are added to this list
    */
    QList<s_audioRoutes> m_audioRoutes;

    void removeAllRoutesFromSlot(int slot);

    /**
    * @brief List all active conference ports
    * @return Struct with names and Slot IDs for Sources and Destinations
    */
    s_audioPortList listConfPorts();

    /**
    * @brief Custom lables for souces are mapped
    * @param key QString pjname
    * @param value QString custom label
    */
    QMap<QString,QString> m_customSourceLabels;
    QMap<QString,QString> m_customDestLabels;
};

#endif // AUDIOROUTER_H
