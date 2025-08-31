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

/**
 * @file webrtcchannels.h
 * @brief Manages non-SIP WebRTC channels and sessions using PJMEDIA transports.
 *
 * Responsibilities:
 * - Channel CRUD with per-channel ICE/STUN/TURN settings (independent of SIP).
 * - Session lifecycle (offer/answer/ICE candidates) using PJMEDIA transports.
 * - Delegates all conference-bridge wiring to AudioRouter via attach/detach.
 * - Emits JSON-friendly status for WebSocket API consumers.
 *
 * Key types:
 * - WebRTCSession: runtime media/ICE/SDP state per session.
 * - s_webrtc_channel: persisted channel config (id acts as a UID).
 */

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
	pj_pool_t* sessionPool = nullptr;            // Per-session pool for per-stream objects

	// SDP negotiation
	pjmedia_sdp_session* remoteSdp = nullptr;
	pjmedia_sdp_session* localSdp = nullptr;
	pjmedia_sdp_neg* sdpNeg = nullptr;

	// Trickle ICE bookkeeping (optional)
	QString remoteUfrag;
	QString remotePwd;
	QStringList pendingIceCandidates;

	// ICE state
	bool iceSessionReady = false;
	bool srtpReady = false;
};

/**
 * @class WebRTCChannels
 * @brief Controller for WebRTC channels/sessions and transport setup.
 *
 * Usage:
 * - createChannel() to add a channel. The provided id is treated as a UID; if empty,
 *   upstream should generate a UID via createNewUID().
 * - createSession() to spawn a new media session under a channel.
 * - processWebRTCOffer/Answer/IceCandidate to drive signaling.
 * - hangupCall()/disconnectWebRTCCall() to teardown.
 */
class WebRTCChannels : public QObject
{
	Q_OBJECT

public:
	/** Construct the controller. */
	explicit WebRTCChannels(AWAHSipLib *parentLib, QObject *parent = nullptr);
	~WebRTCChannels();

	// Channel management - same pattern as SIP accounts
	/**
	 * @brief Create a channel. The id acts as a stable UID.
	 * @param id Unique identifier; when empty a UID will be generated
	 * @param description Human-friendly description
	 * @param enabled Whether the channel is enabled immediately
	 * @return true on success
	 */
	bool createChannel(QString id = "", QString description = "", bool enabled = true);
	/**
	 * @brief Modify channel properties.
	 * @param id Channel UID
	 * @param description New description
	 * @param enabled Enable/disable the channel
	 */
	void modifyChannel(QString id, QString description, bool enabled);
	/**
	 * @brief Remove a channel and hang up all sessions under it.
	 * @param id Channel UID
	 */
	void removeChannel(QString id);
	/**
	 * @brief Get all configured channels.
	 * @return Mutable list of channel configs
	 */
	QList<s_webrtc_channel>* getChannels();
	/**
	 * @brief Lookup channel by id/uid.
	 * @param id Channel UID
	 * @return Pointer to channel or nullptr if not found
	 */
	s_webrtc_channel* getChannelById(QString id);
	
	// WebRTC-specific configuration
	/**
	 * @brief Set per-channel STUN server URL.
	 * @param channelId Channel UID
	 * @param stunServer e.g. "stun:stun.l.google.com:19302"
	 */
	void setChannelStunServer(const QString& channelId, const QString& stunServer);
	/**
	 * @brief Set per-channel TURN config and credentials.
	 * Enables TURN when server is non-empty.
	 * @param channelId Channel UID
	 * @param turnServer e.g. "turn:example.com:3478?transport=udp"
	 * @param username TURN username
	 * @param credential TURN password/credential
	 */
	void setChannelTurnServer(const QString& channelId, const QString& turnServer, 
	                         const QString& username, const QString& credential);
	/**
	 * @brief Configure send-only mode for monitoring channels.
	 * @param channelId Channel UID
	 * @param sendOnly true to disable receiving
	 */
	void setChannelSendOnly(const QString& channelId, bool sendOnly);
	/**
	 * @brief Limit concurrent sessions per channel.
	 * @param channelId Channel UID
	 * @param maxCalls Maximum active sessions allowed
	 */
	void setChannelMaxCalls(const QString& channelId, int maxCalls);
	
	// Session management (non-SIP WebRTC sessions)
	/**
	 * @brief Create a new session under a channel.
	 * @param channelId Channel UID
	 * @return New sessionId or empty on error
	 */
	QString createSession(const QString& channelId);
	/**
	 * @brief Lookup session by id.
	 * @param sessionId Session identifier
	 * @return Pointer to session or nullptr
	 */
	WebRTCSession* getSession(const QString& sessionId);
	/**
	 * @brief Remove session bookkeeping.
	 * Media teardown is handled elsewhere.
	 * @param sessionId Session identifier
	 */
	void removeSession(const QString& sessionId);
	
	// Media control
	/**
	 * @brief Hang up an active session and teardown transports.
	 * @param sessionId Session identifier
	 */
	void hangupCall(const QString& sessionId);
	
	// Trickle ICE support - essential for WebRTC
	/**
	 * @brief Queue a remote ICE candidate for a session.
	 * Applies immediately when transports are ready.
	 * @param sessionId Session identifier
	 * @param candidate Full candidate attribute line (without "a=")
	 * @param sdpMLineIndex m-line index (default 0)
	 * @param sdpMid mid value (default "0")
	 */
	void addIceCandidate(const QString& sessionId, const QString& candidate, 
	                    int sdpMLineIndex = 0, const QString& sdpMid = "0");
	/**
	 * @brief Apply any stored ICE candidates after transport/session is ready.
	 * @param sessionId Session identifier
	 */
	void processStoredIceCandidates(const QString& sessionId);
	
	// High-level WebRTC API for Websocket facade
	/**
	 * @brief Process a remote WebRTC offer and generate a local answer.
	 * @param channelId Channel UID
	 * @param sessionId Session identifier
	 * @param offer JSON-encoded SDP offer
	 * @param response Output object populated with SDP answer and metadata
	 * @return true on success
	 */
	bool processWebRTCOffer(const QString& channelId, const QString& sessionId, 
	                       const QJsonObject& offer, QJsonObject& response);
	/**
	 * @brief Process a remote WebRTC answer for an existing session.
	 * @param sessionId Session identifier
	 * @param answer JSON-encoded SDP answer
	 * @param response Output object with processing results
	 * @return true on success
	 */
	bool processWebRTCAnswer(const QString& sessionId, const QJsonObject& answer, QJsonObject& response);
	/**
	 * @brief Process a remote ICE candidate for an existing session.
	 * @param sessionId Session identifier
	 * @param candidate Candidate attribute value
	 * @param sdpMLineIndex Corresponding m-line index
	 * @param sdpMid Corresponding mid
	 * @return true on success
	 */
	bool processWebRTCIceCandidate(const QString& sessionId, const QString& candidate, 
	                              int sdpMLineIndex, const QString& sdpMid);
	/**
	 * @brief Disconnect an active WebRTC call/session.
	 * @param sessionId Session identifier
	 * @return true on success
	 */
	bool disconnectWebRTCCall(const QString& sessionId);
	/**
	 * @brief Populate a JSON status snapshot for UI/diagnostics.
	 * @param channelId Channel UID (filter) or empty for all
	 * @param response Output object
	 */
	void getWebRTCStatus(const QString& channelId, QJsonObject& response);

signals:
	/** Emitted when channel list changes. */
	void ChannelsChanged(QList<s_webrtc_channel>* channels);
	/** Emitted to deliver ICE candidate to remote peer via WebSocket. */
	void webrtcIceCandidate(QString channelId, QString sessionId, QString candidate, 
	                       int sdpMLineIndex, QString sdpMid);
	/** Emitted with local SDP answer for a given session. */
	void webrtcSdpAnswer(QString channelId, QString sessionId, QJsonObject sdpAnswer);

private:
	// Conference bridge integration (cleanup only)
	/**
	 * @brief Detach and cleanup bridge resources for a session.
	 * @param sessionId Session identifier
	 */
	void disconnectStreamFromChannel(const QString& sessionId);
	
	// Helper methods
	/** Generate a random session identifier. */
	QString generateSessionId();
	/**
	 * @brief Create media objects and local SDP for a remote offer.
	 * @param session Session object to populate
	 * @param channel Channel configuration
	 * @param remoteSdp Remote SDP string
	 * @param outLocalSdp Output local SDP (string)
	 * @return true on success
	 */
	bool createMediaForOffer(WebRTCSession& session, const s_webrtc_channel& channel, const QString& remoteSdp, QString& outLocalSdp);
	/**
	 * @brief Initialize ICE transport with remote ufrag/pwd.
	 * @param session Session object
	 * @param channel Channel configuration
	 * @param remoteUfrag Remote ICE username fragment
	 * @param remotePwd Remote ICE password
	 * @return true on success
	 */
	bool initializeIceTransport(WebRTCSession& session, const s_webrtc_channel& channel,
	                            const QString& remoteUfrag, const QString& remotePwd);
	/**
	 * @brief Add a parsed remote ICE candidate into the session.
	 * @param session Session object
	 * @param candidate Candidate attribute value
	 * @param mlineIndex m-line index
	 * @param mid mid value
	 * @return true on success
	 */
	bool addRemoteIceCandidate(WebRTCSession& session, const QString& candidate, int mlineIndex, const QString& mid);
	/**
	 * @brief Initialize DTLS-SRTP wrapping over ICE transport.
	 * @param session Session object
	 * @return true on success
	 */
	bool initializeDtlsSrtp(WebRTCSession& session);
	/**
	 * @brief Create and start an Opus stream bound to transports.
	 * @param session Session object
	 * @return true on success
	 */
	bool createOpusStream(WebRTCSession& session);
	/**
	 * @brief Parse a candidate string into pj_ice_sess_cand.
	 * @param candStr Candidate attribute value
	 * @param outCand Output parsed candidate
	 * @return true on success
	 */
	bool parseRemoteCandidate(const QString& candStr, pj_ice_sess_cand& outCand);
	/**
	 * @brief Find a session by PJMEDIA transport pointer.
	 * @param tp Transport pointer
	 * @return Session pointer or nullptr
	 */
	WebRTCSession* findSessionByTransport(pjmedia_transport* tp);
	
	// ICE callbacks
	/** ICE op completion callback (legacy signature). */
	static void onIceCompleteCb(pjmedia_transport *tp, pj_ice_strans_op op, pj_status_t status);
	/** ICE op completion callback with user data. */
	static void onIceCompleteCb2(pjmedia_transport *tp, pj_ice_strans_op op, pj_status_t status, void *user_data);
	/** Notified when a new ICE candidate appears. */
	static void onIceNewCandidateCb(pjmedia_transport *tp, const pj_ice_sess_cand *cand, pj_bool_t last);
	
	// SRTP callbacks
	/** SRTP negotiation completion callback. */
	static void onSrtpNegoCompleteCb(pjmedia_transport *tp, pj_status_t status);
	
private:
	AWAHSipLib* m_lib;
	QList<s_webrtc_channel> m_channels;      // Channel configs (ids are UIDs)
	QMap<QString, WebRTCSession> m_sessions; // sessionId -> session
	QMap<QString, int> m_webrtcAudioSlots;   // Track audio slots by sessionId (bridge-facing), if needed
	
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
	/** PJLIB worker thread entry to drive ICE timers/ioqueue. */
	static int webrtcWorkerThread(void* arg);
};

#endif // WEBRTCCHANNELS_H 