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

#ifndef AUDIOROUTER_H
#define AUDIOROUTER_H

#include <QObject>
#include <QMap>
#include "types.h"
#include <QTimer>

// Forward declare pjmedia types to avoid heavy includes in header
struct pjmedia_port;

class AWAHSipLib;

/**
 * @file audiorouter.h
 * @brief Centralized declarative audio routing and conference bridge orchestration.
 *
 * AudioRouter provides a high-level, declarative model for audio routing across SIP calls,
 * WebRTC streams, and anchored audio devices. Components register their media ports and
 * metadata (parent keys and channel indices). Desired routes are stored as s_audioRoutes
 * and a reconciliation engine ensures the pjmedia conference bridge reflects this desired
 * state (connect/disconnect/level adjust), emitting derived ephemeral child routes for UI.
 *
 * Key concepts:
 * - Parent/Child model: Parents (e.g. SIP account ACC:<uid>, WebRTC channel WRTC_CH:<id>)
 *   own zero or more child conf slots (per-channel). Routes may target parents or devices;
 *   parent routes are expanded to child routes during reconcile.
 * - BridgeAdapter/Channelizer: attachStreamChannels() exposes per-channel mono conf slots for
 *   SIP/WebRTC streams (via splitcomb/master-port for stereo+) and registers them to parents.
 * - Managed media assets: unified creation/teardown of announcement file players and
 *   per-call recorders, registered per-parent and wired via reconcile (e.g. recorder mirroring).
 * - Virtual slots: deterministic negative slot ids are used for virtual parents and offline
 *   SoundDevices so routes remain visible and persist even when no real slots exist.
 * - Reconciliation: compares desired connections with actual bridge state, applying
 *   connect/disconnect/adjust operations and publishing enriched routes to the UI.
 */
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
    * @brief get the offline audio routes
    * @return the audioRoutes struct
    */
    QList <s_audioRoutes> getOfflineAudioRoutes() { return m_offlineRoutes; };

    /**
    * @brief If a route referes to a device that is offline add it here to restore it if the device is online again
    * @param route the offline route
    */
    void addOfflineAudioRoute(s_audioRoutes route){m_offlineRoutes.append(route);};

    /**
    * @brief clear all offline routes
    */
    void clearAllOfflineAudioRoutes(){m_offlineRoutes.clear();};

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
    * @brief Set the Clocking device
    *        the timing for the conference bridge is taken from this device.
    * @param recordDevId ID of the desired record device
    * @param playbackDevId ID of the dseired playback device
    * @param if the device is loaded from settings you have to provide it's uid to assign the correct routes
    */
    void AddClockingDevice(int recordDevId, int playbackDevId, QString uid);

    /**
    * @brief Add an Audio device to the conference bridge
    *        all channels of the device will be added (upto 64 channels)
    * @param recordDevId ID of the desired record device
    * @param playbackDevId ID of the dseired playback device
    * @param if the device is loaded from settings you have to provide it's uid to assign the correct routes
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
    void setAudioDeviceToOffline(QString inputName, QString outputName, QString uid);

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
    * @param File the path                                              // todo explain in more detail
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
     * @brief Remove a conference port from the bridge safely
     * @param slot Real conference-bridge slot id
     * @return PJ_SUCCESS or error code
     */
    int removeConfPort(int slot);

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
     * @brief Remove all connections where the given slot participates (as source or destination)
     *        Used on SIP/WebRTC teardown to ensure no stale crosspoints remain.
     * @param slot the conference-bridge slot whose routes should be removed
     */
    void removeAllRoutesFromSlot(int slot);

    /**
    * @brief get the active devices
    * @return the AudioDevice struct
    */
    QList<s_IODevices>* getAudioDevices() { return &m_AudioDevices; };

    void removeAllRoutesFromAccount(const s_account account);
    // Remove all parent-intent routes and child-ephemeral routes for a parent key (ACC:..., WRTC_CH:...)
    void removeAllRoutesForParentKey(const QString &parentKey);
    void scheduleConferenceRefresh(int delayMs);
    void scheduleRouteEvaluation(int delayMs = 200);

    QMap<int, QString> getSrcAudioSlotMap() const { return m_srcAudioSlotMap; };
    QMap<int, QString> getDestAudioSlotMap() const { return m_destAudioSlotMap; };
    QMap<QString, QString> getCustomSourceLabels() const { return m_customSourceLabels; };
    QMap<QString, QString> getCustomDestLables() const { return m_customDestLabels; };
    /**
     * @brief Set custom labels for sources
     * @param srclables map of pj names to custom labels
     */
    void setCustomSourceLables(const QMap<QString, QString> srclables) { m_customSourceLabels = srclables; };
    /**
     * @brief Set custom labels for destinations
     * @param dstlables map of pj names to custom labels
     */
    void setCustomDestinationLables(const QMap<QString, QString> dstlables) { m_customDestLabels = dstlables; };
    /**
     * @brief Force-refresh conference ports and update internal slot maps
     */
    void refreshConfPortMaps();

    // Parent/child AudioMatrix extensions
    // Register a new conf slot as a child of a parent entity (account/channel). Use stable keys like
    // "ACC:<uid>" or "WRTC_CH:<channelId>". parentType hints GUI grouping.
    /**
     * @brief Register a conference-bridge child slot under a logical parent (account/channel)
     * @param slot Real conference-bridge slot id
     * @param parentType Parent entity type
     * @param parentKey Stable parent key, e.g. "ACC:<uid>" or "WRTC_CH:<id>"
     */
    void registerParentForSlot(int slot, AudioEntityType parentType, const QString &parentKey);
    // Overload with explicit channel index (1-based; 0 means unknown)
    /**
     * @brief Register a child slot with explicit channel index metadata (1-based)
     * @param slot Real conference-bridge slot id
     * @param parentType Parent entity type
     * @param parentKey Stable parent key
     * @param channelIndex 1-based channel index; 0 if unknown
     */
    void registerParentForSlot(int slot, AudioEntityType parentType, const QString &parentKey, int channelIndex);
    /**
     * @brief Unregister a previously registered real slot (on teardown)
     * @param slot Real conference-bridge slot id
     */
    void unregisterSlot(int slot);
    AudioEntityType getParentTypeForSlot(int slot) const { return m_slotToParentType.value(slot, Entity_Unknown); }
    QString getParentKeyForSlot(int slot) const { return m_slotToParentKey.value(slot, QString()); }
    QList<int> getChildSlotsForParent(const QString &parentKey) const { return m_parentKeyToChildSlots.value(parentKey); }
    /**
     * @brief Retrieve stored channel index for a real slot (fallback 0 if unknown)
     */
    int getChannelIndexForSlot(int slot) const { return m_slotChannelIndex.value(slot, 0); }

    // BridgeAdapter / channelizer APIs
    // sessionKey should be unique per call/session, e.g. "ACC:<uid>|CID:<id>"
    // accountName and remoteNumber are used for user-friendly labeling
    /**
     * @brief Attach a multi-channel media stream to the conference bridge and expose per-channel slots
     * @param parentKey Logical parent key (e.g. "ACC:<uid>")
     * @param sessionKey Unique per-session key (e.g. "ACC:<uid>|CID:<callId>")
     * @param accountName Friendly label part
     * @param remoteNumber Friendly label part
     * @param streamPort pjmedia stream media port
     * @param channelCount Number of channels in the stream (>=1)
     */
    void attachStreamChannels(const QString &parentKey,
                              const QString &sessionKey,
                              const QString &accountName,
                              const QString &remoteNumber,
                              pjmedia_port *streamPort,
                              int channelCount);
    /**
     * @brief Detach and clean up all bridge resources created for a session
     * @param sessionKey Unique per-session key used in attachStreamChannels()
     */
    void detachParentStream(const QString &sessionKey);

    // Recorder registry per parent (e.g., ACC:<uid>) to accelerate reconcile
    /**
     * @brief Register a recorder conference slot for a parent
     * @param parentKey Parent key (e.g. "ACC:<uid>")
     * @param recorderConfSlot Recorder's conference-bridge slot id
     */
    void registerRecorderForParent(const QString &parentKey, int recorderConfSlot) { if (!parentKey.isEmpty()) m_parentToRecorderSlot[parentKey] = recorderConfSlot; }
    /**
     * @brief Register a recorder slot and its backend id for a parent
     * @param parentKey Parent key
     * @param recorderConfSlot Recorder conference slot id
     * @param recorderId pjsua_recorder_id
     */
    void registerRecorderForParent(const QString &parentKey, int recorderConfSlot, int recorderId) { if (!parentKey.isEmpty()) { m_parentToRecorderSlot[parentKey] = recorderConfSlot; m_parentToRecorderId[parentKey] = recorderId; } }
    /**
     * @brief Unregister any recorder mapping for a parent
     * @param parentKey Parent key
     */
    void unregisterRecorderForParent(const QString &parentKey) { if (!parentKey.isEmpty()) m_parentToRecorderSlot.remove(parentKey); }
    /**
     * @brief Get recorder conference slot for a parent, or PJSUA_INVALID_ID
     * @param parentKey Parent key
     * @return Recorder slot id or PJSUA_INVALID_ID
     */
    int getRecorderForParent(const QString &parentKey) const { return m_parentToRecorderSlot.value(parentKey, PJSUA_INVALID_ID); }
    // Announcement/file player registry
    /**
     * @brief Register a one-shot announcement player's conference slot for a parent
     * @param parentKey Parent key
     * @param playerConfSlot Player's conference slot
     */
    void registerPlayerForParent(const QString &parentKey, int playerConfSlot) { if (!parentKey.isEmpty()) m_parentToPlayerSlot[parentKey] = playerConfSlot; }
    /**
     * @brief Unregister a player's mapping for a parent
     * @param parentKey Parent key
     */
    void unregisterPlayerForParent(const QString &parentKey) { if (!parentKey.isEmpty()) m_parentToPlayerSlot.remove(parentKey); }
    /**
     * @brief Get player conference slot for a parent, or PJSUA_INVALID_ID
     * @param parentKey Parent key
     * @return Player slot id or PJSUA_INVALID_ID
     */
    int getPlayerForParent(const QString &parentKey) const { return m_parentToPlayerSlot.value(parentKey, PJSUA_INVALID_ID); }
    /**
     * @brief Create a one-shot announcement player for a parent
     * @param parentKey Parent key
     * @param filePath Audio file path to play once
     * @param uid Optional stable identifier to include in labels
     * @return pjsua_player_id or PJSUA_INVALID_ID
     */
    int addAnnouncementPlayerForParent(const QString &parentKey, const QString &filePath, const QString &uid = "");
    /**
     * @brief Internal: called when announcement playback finishes for a parent
     * @param parentKey Parent key
     */
    void onAnnouncementFinished(const QString &parentKey);
    /**
     * @brief Create a per-call file recorder for a parent
     * @param parentKey Parent key
     * @param filePath Destination file path
     * @param uid Optional identifier (unused)
     * @return pjsua_recorder_id or PJSUA_INVALID_ID
     */
    int addCallRecorderForParent(const QString &parentKey, const QString &filePath, const QString &uid = "");
    /**
     * @brief Teardown announcement player and/or call recorder owned by a parent
     * @param parentKey Parent key
     */
    void teardownParentMedia(const QString &parentKey);

    // Parent-level routing via unified s_audioRoutes API
    /**
     * @brief Connect a parent to another parent or device declaratively via s_audioRoutes
     * @param parentKey Source or destination parent key
     * @param parentType Parent entity type
     * @param parentAsSource True if parent is the source, false otherwise
     * @param otherParentOrDev Other endpoint (parent key or device name)
     * @param level Level in dB
     * @param persistant Persist the route
     * @param channelIndex Optional source-channel index (1-based); 0 means any
     * @param otherChannelIndex Optional dest-channel index (1-based); 0 means any
     * @return PJ_SUCCESS or error code
     */
    int connectParentToDev(const QString &parentKey, AudioEntityType parentType, bool parentAsSource,
                           const QString &otherParentOrDev, int level, bool persistant,
                           int channelIndex = 0, int otherChannelIndex = 0);

    // Expose reconciliation so other components (e.g., PJCall) can force a refresh immediately
    /**
     * @brief Force immediate reconcile and UI update
     */
    void reconcileActiveRoutesNow();

    // Determine the appropriate level for mirroring a child slot to the recorder,
    // using routes where the given parentKey is the destination parent.
    // If requiredDestChannel > 0, prefer routes that target that specific channel;
    // otherwise allow channel-agnostic routes (destParentChannel==0). Falls back to defaultLevel.
    /**
     * @brief Compute appropriate mirror level for a child slot when routing to a recorder
     * @param childSlot Real child slot id
     * @param parentKey Destination parent key (ACC:<uid>)
     * @param requiredDestChannel 1-based destination channel to match; 0 means any
     * @param defaultLevel Fallback level in dB
     * @return Level in dB
     */
    int getMirroredLevelForChild(int childSlot, const QString &parentKey,
                                 int requiredDestChannel = 1,
                                 int defaultLevel = 0) const;

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
    * @brief Signal if a level of a confport has changed
    * @param audioRoutes the Route with the changed level
    */
    void confportLevelChanged(s_audioRoutes changedRoute);

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
    QTimer *m_SoundDeviceInspectorTimer;
    QTimer *m_confRefreshDebounceTimer = nullptr;
    QTimer *m_routeEvalDebounceTimer = nullptr;
    uint8_t m_sounddevCount = 0;
    pjmedia_master_port *themaster = nullptr;
    void conferenceBridgeChanged();
    // Reconcile m_audioRoutes with actual connections present in the conference bridge
    bool reconcileActiveConnections();

    /**
    * @brief All AudioDevices (soundcards, generators, fileplayer and recoder) are added to this list
    *       in order to save and load current audio setup
    */
    QList<s_IODevices> m_AudioDevices;

    /**
    * @brief All routes from the conference bridge are added to this list
    */
    QList<s_audioRoutes> m_audioRoutes;

    /**
    * @brief offline routes are stored here
    * @details we have to keep track of offline routes, oterwise they get lost on restart
    * this list is then added to te settingsfile with the online routes
    */
    QList<s_audioRoutes>  m_offlineRoutes;
    // Ephemeral, derived child routes built by reconcile for UI/inspection
    QList<s_audioRoutes>  m_ephemeralRoutes;


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

    // Parent/child mappings
    QMap<int, QString> m_slotToParentKey;                 // slot -> parent key (e.g., ACC:<uid>, WRTC_CH:<id>)
    QMap<int, AudioEntityType> m_slotToParentType;        // slot -> parent type
    QMap<QString, QList<int>> m_parentKeyToChildSlots;    // parent key -> child slots
    // no separate parent route list; parent routes live in m_audioRoutes using parent fields
    // Virtual slot registry (negative IDs) for GUI selection of parents
    QMap<QString, int> m_virtualKeyToSlot;                // key: parentKey|src/dst|ch
    QMap<int, QString> m_virtualSlotToParentKey;          // neg slot -> parentKey
    QMap<int, AudioEntityType> m_virtualSlotToEntityType; // neg slot -> entityType
    QMap<int, int> m_virtualSlotToChannel;                // neg slot -> 1 or 2
    QMap<int, bool> m_virtualSlotIsSource;                // neg slot -> is source side
    int m_nextVirtualSlot = -1;                           // decreasing allocator
    // Virtual slots for anchored devices (child-only) when offline
    QMap<int, QString> m_virtualDeviceSlotToName;         // neg slot -> device name string (e.g., AD:uid-Ch:n / FP:uid-Name / FR:...)
    // Real slot -> channel index (1..N), 0 unknown
    QMap<int,int> m_slotChannelIndex;
    // Parent key -> recorder conf slot (if active)
    QMap<QString,int> m_parentToRecorderSlot;
    QMap<QString,int> m_parentToPlayerSlot;
    // Track player ids to allow destruction on EOF
    QMap<QString,int> m_parentToPlayerId;
    // Track recorder ids to allow destruction on teardown
    QMap<QString,int> m_parentToRecorderId;

    // Per-session channelizer resources
    QHash<QString, QList<int>> m_sessionToConfSlots;
    QHash<QString, QList<pjmedia_port*>> m_sessionToRevPorts;
    QHash<QString, pjmedia_master_port*> m_sessionToMaster;
    QHash<QString, pjmedia_port*> m_sessionToSplitcomb;
    QHash<QString, pj_pool_t*> m_sessionToPool;

    // Apply stored parent-level routes when a child slot becomes available
    void applyParentRoutesToChildSlot(int slot, AudioEntityType parentType, const QString &parentKey);
    int getOrCreateVirtualSlot(const QString &parentKey, AudioEntityType type, bool isSource, int channelIndex);
    void emitRoutesChanged();
    // Virtual slots for child-only devices (AD:/FP:/FR: names) when offline
    int getOrCreateVirtualDeviceSlot(const QString &devName, bool isSource);
    // Internal helpers to create managed PJSUA media and add to bridge
    int createManagedPlayer(const QString &portLabel, const QString &filePath,
                            unsigned playerFlags, pjmedia_port **outMediaPort,
                            int &outConfSlot);
    int createManagedRecorder(const QString &portLabel, const QString &filePath,
                              pjmedia_port **outMediaPort, int &outConfSlot);

private slots:
    /**
    * @brief SoundDeviceInspector checks the avaliable sound devices, if there is a change in the system
    * @brief the devices in use ar checked if they are still availabe and the offline devices a checked if they are online now
    * @brief with this sound devices are hot pluggable
    */
    void SoundDeviceInspector();
};

#endif // AUDIOROUTER_H
