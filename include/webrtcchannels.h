#ifndef WEBRTCCHANNELS_H
#define WEBRTCCHANNELS_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QStringList>
#include <pjsua-lib/pjsua.h>
#include <pj/pool.h>
#include <pjlib.h>
#include <QMap>
#include "types.h"
#include <pjmedia.h>
#include <pjmedia/stream.h>
#include <pjmedia/port.h>
#include <pjmedia/transport_ice.h>
#include <pjmedia/transport_srtp.h>
#include <pjmedia/master_port.h>
#include <pjmedia/splitcomb.h>

class AWAHSipLib;

struct WebRTCSession {
    QString sessionId;
    QString channelId;
    QDateTime startTime;
    bool isActive = false;

    // pjmedia objects for non-SIP WebRTC
    pjmedia_transport* iceTransport = nullptr;   // ICE transport
    pjmedia_transport* srtpTransport = nullptr;  // SRTP/DTLS wrapper
    pjmedia_stream* mediaStream = nullptr;       // Audio stream
    pjmedia_port* streamPort = nullptr;          // Port interface of the stream
    pjmedia_master_port* mp_split_to_stream = nullptr; // splitcomb -> stream (send path)
    pjmedia_master_port* mp_stream_to_split = nullptr; // stream -> splitcomb (receive path)
    pjmedia_port* perStreamSplitComb = nullptr;        // Per-stream split/comb (matches stream)
    QList<pjmedia_port*> perStreamMonoPorts;           // Reverse-channel mono ports
    QList<int> perStreamConfSlots;                     // Conf slots for mono ports
    pj_pool_t* sessionPool = nullptr;                  // Per-session pool for per-stream objects

    // SDP negotiation
    pjmedia_sdp_session* remoteSdp = nullptr;
    pjmedia_sdp_session* localSdp = nullptr;
    pjmedia_sdp_neg* sdpNeg = nullptr;

    // Conference bridge integration
    int confSlot = -1;        // Conference bridge slot for this stream
    int splitterSlot = -1;    // Connected to channel's splitter-combiner

    // Trickle ICE bookkeeping (optional)
    QString remoteUfrag;
    QString remotePwd;
    QStringList pendingIceCandidates;

    // ICE state
    bool iceSessionReady = false;
    bool srtpReady = false;
};

// s_webrtc_channel is defined in types.h

/**
 * @brief WebRTCChannels manages WebRTC calls using PJSUA high-level APIs
 * 
 * This class follows the same pattern as the existing SIP Accounts class:
 * - Each WebRTC channel has its own PJSUA account with WebRTC-specific settings
 * - Each call uses PJSUA call APIs for proper SDP generation and ICE handling
 * - Audio routing uses the same splitter-combiner pattern as SIP accounts
 * - Supports multiple calls per channel and audio-only monitoring channels
 */
class WebRTCChannels : public QObject
{
    Q_OBJECT

public:
    explicit WebRTCChannels(AWAHSipLib *parentLib, QObject *parent = nullptr);
    ~WebRTCChannels();

    // Channel management - same pattern as SIP accounts
    bool createChannel(QString id, QString description, bool enabled = true);
    void modifyChannel(QString id, QString description, bool enabled);
    void removeChannel(QString id);
    QList<s_webrtc_channel>* getChannels();
    s_webrtc_channel* getChannelById(QString id);
    
    // WebRTC-specific configuration
    void setChannelStunServer(const QString& channelId, const QString& stunServer);
    void setChannelTurnServer(const QString& channelId, const QString& turnServer, 
                              const QString& username, const QString& credential);
    void setChannelSendOnly(const QString& channelId, bool sendOnly);
    void setChannelMaxCalls(const QString& channelId, int maxCalls);
    
    // Session management (non-SIP WebRTC sessions)
    QString createSession(const QString& channelId);
    WebRTCSession* getSession(const QString& sessionId);
    void removeSession(const QString& sessionId);
    
    // Media control
    void hangupCall(const QString& sessionId);
    
    // Trickle ICE support - essential for WebRTC
    void addIceCandidate(const QString& sessionId, const QString& candidate, 
                        int sdpMLineIndex = 0, const QString& sdpMid = "0");
    void processStoredIceCandidates(const QString& sessionId);
    
    // High-level WebRTC API for Websocket facade
    bool processWebRTCOffer(const QString& channelId, const QString& sessionId, 
                           const QJsonObject& offer, QJsonObject& response);
    bool processWebRTCAnswer(const QString& sessionId, const QJsonObject& answer, QJsonObject& response);
    bool processWebRTCIceCandidate(const QString& sessionId, const QString& candidate, 
                                  int sdpMLineIndex, const QString& sdpMid);
    bool disconnectWebRTCCall(const QString& sessionId);
    void getWebRTCStatus(const QString& channelId, QJsonObject& response);

signals:
    void ChannelsChanged(QList<s_webrtc_channel>* channels);
    void webrtcIceCandidate(QString channelId, QString sessionId, QString candidate, 
                           int sdpMLineIndex, QString sdpMid);
    void webrtcSdpAnswer(QString channelId, QString sessionId, QJsonObject sdpAnswer);

private:
    // Conference bridge integration (cleanup only)
    void disconnectStreamFromChannel(const QString& sessionId);
    
    // Helper methods
    QString generateSessionId();
    bool createMediaForOffer(WebRTCSession& session, const s_webrtc_channel& channel, const QString& remoteSdp, QString& outLocalSdp);
    bool initializeIceTransport(WebRTCSession& session, const s_webrtc_channel& channel,
                                const QString& remoteUfrag, const QString& remotePwd);
    bool addRemoteIceCandidate(WebRTCSession& session, const QString& candidate, int mlineIndex, const QString& mid);
    bool initializeDtlsSrtp(WebRTCSession& session);
    bool createOpusStream(WebRTCSession& session);
    bool parseRemoteCandidate(const QString& candStr, pj_ice_sess_cand& outCand);
    WebRTCSession* findSessionByTransport(pjmedia_transport* tp);

    // ICE callbacks (to be wired later)
    static void onIceCompleteCb(pjmedia_transport *tp, pj_ice_strans_op op, pj_status_t status);
    static void onIceCompleteCb2(pjmedia_transport *tp, pj_ice_strans_op op, pj_status_t status, void *user_data);
    static void onIceNewCandidateCb(pjmedia_transport *tp, const pj_ice_sess_cand *cand, pj_bool_t last);

    // SRTP callbacks
    static void onSrtpNegoCompleteCb(pjmedia_transport *tp, pj_status_t status);

private:
    AWAHSipLib* m_lib;
    QList<s_webrtc_channel> m_channels;
    QMap<QString, WebRTCSession> m_sessions;
    QMap<QString, int> m_webrtcAudioSlots;        // Track audio slots by sessionId
    
    // Static instance for callbacks
    static WebRTCChannels* s_instance;

    // PJLIB resources for ICE/STUN
    pj_caching_pool m_webrtcCp;
    pj_pool_t* m_webrtcPool = nullptr;
    pj_ioqueue_t* m_webrtcIoqueue = nullptr;
    pj_timer_heap_t* m_webrtcTimer = nullptr;
    // Background PJLIB thread to poll timers/ioqueue for standalone ICE
    pj_thread_t* m_webrtcThread = nullptr;
    pj_bool_t m_webrtcThreadQuit = PJ_FALSE;
    static int webrtcWorkerThread(void* arg);
};

#endif // WEBRTCCHANNELS_H 