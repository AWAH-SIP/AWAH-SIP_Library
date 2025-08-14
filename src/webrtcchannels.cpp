#include "../include/webrtcchannels.h"
#include "../include/awahsiplib.h"
#include "../include/log.h"
#include <QDateTime>
#include <QUuid>
#include <QDebug>
 #include <QStringBuilder>
#include <pjsua-lib/pjsua.h>
#include <pjsua-lib/pjsua_internal.h>
#include <QRandomGenerator>
#include <pj/math.h>

// Static instance for PJSUA callbacks
WebRTCChannels* WebRTCChannels::s_instance = nullptr;

WebRTCChannels::WebRTCChannels(AWAHSipLib *parentLib, QObject *parent)
    : QObject(parent), m_lib(parentLib)
{
    s_instance = this;
    m_lib->m_Log->writeLog(3, "WebRTCChannels: Initialized (non-SIP WebRTC mode)");

    // Init standalone PJLIB resources for ICE/STUN
    pj_caching_pool_init(&m_webrtcCp, &pj_pool_factory_default_policy, 0);
    m_webrtcPool = pj_pool_create(&m_webrtcCp.factory, "webrtc", 4096, 4096, nullptr);
    pj_status_t s;
    s = pj_timer_heap_create(m_webrtcPool, 16, &m_webrtcTimer);
    if (s != PJ_SUCCESS) m_webrtcTimer = nullptr;
    s = pj_ioqueue_create(m_webrtcPool, 16, &m_webrtcIoqueue);
    if (s != PJ_SUCCESS) m_webrtcIoqueue = nullptr;

    // Start background PJLIB thread to poll ICE timers and ioqueue
    if (m_webrtcIoqueue && m_webrtcTimer) {
        pj_status_t st = pj_thread_create(m_webrtcPool, "webrtc-io", &WebRTCChannels::webrtcWorkerThread,
                                          this, 0, 0, &m_webrtcThread);
        if (st != PJ_SUCCESS) {
            m_lib->m_Log->writeLog(1, QString("Failed to create PJ thread for ICE polling: %1").arg(st));
            m_webrtcThread = nullptr;
        }
    }
}

WebRTCChannels::~WebRTCChannels()
{
    // Clean up all sessions
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        hangupCall(it.key());
    }
    
    // Stop PJ thread
    if (m_webrtcThread) {
        m_webrtcThreadQuit = PJ_TRUE;
        pj_thread_join(m_webrtcThread);
        pj_thread_destroy(m_webrtcThread);
        m_webrtcThread = nullptr;
    }

    if (m_webrtcIoqueue) { pj_ioqueue_destroy(m_webrtcIoqueue); m_webrtcIoqueue = nullptr; }
    if (m_webrtcTimer) { pj_timer_heap_destroy(m_webrtcTimer); m_webrtcTimer = nullptr; }
    if (m_webrtcPool) { pj_pool_release(m_webrtcPool); m_webrtcPool = nullptr; }
    pj_caching_pool_destroy(&m_webrtcCp);
    
    s_instance = nullptr;
}

bool WebRTCChannels::createChannel(QString id, QString description, bool enabled)
{
    s_webrtc_channel channel;
    channel.id = id;
    channel.description = description; 
    channel.enabled = enabled;
    
    // Set default WebRTC settings - independent from SIP settings
    channel.iceEnabled = true;
    channel.trickleIceEnabled = true;
    channel.iceAlwaysUpdate = true;
    channel.turnEnabled = false;
    channel.sendOnly = false;
    channel.maxConcurrentStreams = 5;
    channel.stunServer = "stun:stun.l.google.com:19302"; // Default STUN server
    
    // If channel is disabled, just add it to the list without creating infrastructure
    if (!enabled) {
        m_channels.append(channel);
        m_lib->m_Log->writeLog(3, QString("WebRTC channel created (disabled): %1").arg(id));
        emit ChannelsChanged(&m_channels);
        m_lib->m_Settings->saveWebRTCChannelConfig();
        return true;
    }
    
    // Append first so getChannelById(id) works during creation
    const int insertedIndex = m_channels.size();
            m_channels.append(channel);

    // No channel-level splitter/combiner; per-stream splitcombs will be created dynamically
    m_lib->m_Log->writeLog(3, QString("WebRTC channel created: %1").arg(id));
            emit ChannelsChanged(&m_channels);
    m_lib->m_Settings->saveWebRTCChannelConfig();
            return true;
}

void WebRTCChannels::modifyChannel(QString id, QString description, bool enabled)
{
    for (int i = 0; i < m_channels.size(); i++) {
        if (m_channels[i].id == id) {
            m_channels[i].description = description;
            m_channels[i].enabled = enabled;
            
            m_lib->m_Log->writeLog(3, QString("WebRTC channel modified: %1").arg(id));
            emit ChannelsChanged(&m_channels);
            m_lib->m_Settings->saveWebRTCChannelConfig();
            return;
        }
    }
}

void WebRTCChannels::removeChannel(QString id)
{
    // Hangup all calls for this channel
    QList<QString> sessionsToRemove;
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it.value().channelId == id) {
            sessionsToRemove.append(it.key());
        }
    }
    
    for (const QString& sessionId : sessionsToRemove) {
        hangupCall(sessionId);
    }
    
    // No channel-level splitter/combiner anymore
    
    // Remove from channels list
    for (int i = 0; i < m_channels.size(); i++) {
        if (m_channels[i].id == id) {
            m_channels.removeAt(i);
            break;
        }
    }
    
    m_lib->m_Log->writeLog(3, QString("WebRTC channel removed: %1").arg(id));
    emit ChannelsChanged(&m_channels);
    m_lib->m_Settings->saveWebRTCChannelConfig();
}

QList<s_webrtc_channel>* WebRTCChannels::getChannels()
{
    return &m_channels;
}

s_webrtc_channel* WebRTCChannels::getChannelById(QString id)
{
    for (int i = 0; i < m_channels.size(); i++) {
        if (m_channels[i].id == id) {
            return &m_channels[i];
        }
    }
    return nullptr;
}

// WebRTC-specific configuration methods
void WebRTCChannels::setChannelStunServer(const QString& channelId, const QString& stunServer)
{
    s_webrtc_channel* channel = getChannelById(channelId);
    if (channel) {
        channel->stunServer = stunServer;
        m_lib->m_Log->writeLog(3, QString("Set STUN server for channel %1: %2").arg(channelId, stunServer));
    }
}

void WebRTCChannels::setChannelTurnServer(const QString& channelId, const QString& turnServer, 
                                         const QString& username, const QString& credential)
{
    s_webrtc_channel* channel = getChannelById(channelId);
    if (channel) {
        channel->turnServer = turnServer;
        channel->turnUsername = username;
        channel->turnCredential = credential;
        channel->turnEnabled = !turnServer.isEmpty();
        m_lib->m_Log->writeLog(3, QString("Set TURN server for channel %1: %2").arg(channelId, turnServer));
    }
}

void WebRTCChannels::setChannelSendOnly(const QString& channelId, bool sendOnly)
{
    s_webrtc_channel* channel = getChannelById(channelId);
    if (channel) {
        channel->sendOnly = sendOnly;
        m_lib->m_Log->writeLog(3, QString("Set send-only for channel %1: %2").arg(channelId).arg(sendOnly));
    }
}

void WebRTCChannels::setChannelMaxCalls(const QString& channelId, int maxCalls)
{
    s_webrtc_channel* channel = getChannelById(channelId);
    if (channel) {
        channel->maxConcurrentStreams = maxCalls;
        m_lib->m_Log->writeLog(3, QString("Set max calls for channel %1: %2").arg(channelId).arg(maxCalls));
    }
}

// Session management (non-SIP)
QString WebRTCChannels::createSession(const QString& channelId)
{
    s_webrtc_channel* channel = getChannelById(channelId);
    if (!channel || !channel->enabled) {
        m_lib->m_Log->writeLog(1, QString("Cannot create session: channel %1 not found or disabled").arg(channelId));
        return QString();
    }
    
    // Enforce max concurrent streams (count streams that are active or in progress)
    int currentStreams = 0;
    for (const auto& sess : m_sessions) {
        if (sess.channelId != channelId) continue;
        if (sess.isActive || sess.mediaStream != nullptr || sess.srtpReady) {
            currentStreams++;
        }
    }
    if (currentStreams >= channel->maxConcurrentStreams) {
        m_lib->m_Log->writeLog(1, QString("Cannot create session: channel %1 reached max streams (%2)")
                               .arg(channelId).arg(channel->maxConcurrentStreams));
        return QString();
    }
    
    QString sessionId = generateSessionId();
    
    WebRTCSession session;
    session.sessionId = sessionId;
    session.channelId = channelId;
    session.startTime = QDateTime::currentDateTime();
    session.isActive = false;
    session.confSlot = -1;
    session.splitterSlot = channel->splitterSlot;
    
    m_sessions[sessionId] = session;
    
    m_lib->m_Log->writeLog(3, QString("WebRTC session created: %1 for channel %2").arg(sessionId, channelId));

    // Pre-initialize ICE transport ASAP to allow STUN resolution before offer arrives
    WebRTCSession* newSession = getSession(sessionId);
    if (newSession) {
        initializeIceTransport(*newSession, *channel, QString(), QString());
    }
    return sessionId;
}

WebRTCSession* WebRTCChannels::getSession(const QString& sessionId)
{
    if (m_sessions.contains(sessionId)) {
        return &m_sessions[sessionId];
    }
    return nullptr;
}

void WebRTCChannels::removeSession(const QString& sessionId)
{
    if (m_sessions.contains(sessionId)) {
        hangupCall(sessionId);
        m_sessions.remove(sessionId);
        m_lib->m_Log->writeLog(3, QString("WebRTC session removed: %1").arg(sessionId));
    }
}

// Call management using PJSUA call APIs - proper SDP generation and ICE

void WebRTCChannels::hangupCall(const QString& sessionId)
{
    WebRTCSession* session = getSession(sessionId);
    if (!session) {
        return;
    }
    // Disconnect from conference bridge (non-SIP)
    disconnectStreamFromChannel(sessionId);
    m_lib->m_Log->writeLog(3, QString("WebRTC session closed: %1").arg(sessionId));
    session->isActive = false;
    
    // Remove audio slot tracking
    if (m_webrtcAudioSlots.contains(sessionId)) {
        m_webrtcAudioSlots.remove(sessionId);
    }
}


// Trickle ICE support - essential for WebRTC
void WebRTCChannels::addIceCandidate(const QString& sessionId, const QString& candidate, 
                                    int sdpMLineIndex, const QString& sdpMid)
{
    WebRTCSession* session = getSession(sessionId);
    if (!session) {
        m_lib->m_Log->writeLog(1, QString("Cannot add ICE candidate: session %1 not found").arg(sessionId));
        return;
    }
    
    // Handle end-of-candidates indication from browser (null/empty)
    const QString candTrim = candidate.trimmed();
    if (candTrim.isEmpty() || candTrim.compare("null", Qt::CaseInsensitive) == 0 || candTrim.contains("end-of-candidates")) {
        if (session->iceTransport) {
            pj_str_t rufrag = pj_str(const_cast<char*>(session->remoteUfrag.toUtf8().data()));
            pj_str_t rpwd   = pj_str(const_cast<char*>(session->remotePwd.toUtf8().data()));
            pjmedia_ice_trickle_update(session->iceTransport, &rufrag, &rpwd, 0, nullptr, PJ_TRUE);
            m_lib->m_Log->writeLog(3, QString("ICE: end-of-candidates signalled for session %1").arg(sessionId));
        }
        return;
    }
    
    bool queued = false;
    // If ICE transport exists, add immediately; otherwise store for later
    if (session->iceTransport && session->iceSessionReady) {
        if (!addRemoteIceCandidate(*session, candTrim, sdpMLineIndex, sdpMid)) {
        session->pendingIceCandidates.append(QString("%1:%2:%3").arg(candidate).arg(sdpMLineIndex).arg(sdpMid));
            queued = true;
        }
    } else {
        session->pendingIceCandidates.append(QString("%1:%2:%3").arg(candTrim).arg(sdpMLineIndex).arg(sdpMid));
        queued = true;
    }
    if (queued) {
        m_lib->m_Log->writeLog(3, QString("Stored ICE candidate for session %1").arg(sessionId));
    }
}

void WebRTCChannels::processStoredIceCandidates(const QString& sessionId)
{
    WebRTCSession* session = getSession(sessionId);
    if (!session || session->pendingIceCandidates.isEmpty()) {
        return;
    }
    
    for (const QString& candidateData : session->pendingIceCandidates) {
        QStringList parts = candidateData.split(":");
        if (parts.size() >= 3) {
            QString candidate = parts[0];
            int sdpMLineIndex = parts[1].toInt();
            QString sdpMid = parts[2];
            addIceCandidate(sessionId, candidate, sdpMLineIndex, sdpMid);
        }
    }
    
    session->pendingIceCandidates.clear();
    m_lib->m_Log->writeLog(3, QString("Processed stored ICE candidates for session %1").arg(sessionId));
}

// High-level WebRTC API for Websocket facade
bool WebRTCChannels::processWebRTCOffer(const QString& channelId, const QString& sessionId, 
                                       const QJsonObject& offer, QJsonObject& response)
{
    // Create session if it doesn't exist
    QString actualSessionId = sessionId;
    if (actualSessionId.isEmpty()) {
        actualSessionId = createSession(channelId);
        if (actualSessionId.isEmpty()) {
            response["error"] = QString("Failed to create session for channel %1").arg(channelId);
            return false;
        }
    }
    
    WebRTCSession* session = getSession(actualSessionId);
    if (!session) {
        response["error"] = QString("Session %1 not found").arg(actualSessionId);
        return false;
    }
    
    // Extract SDP from offer
    if (!offer.contains("sdp")) {
        response["error"] = "SDP is required in WebRTC offer";
        return false;
    }
    
    QString remoteSdp = offer["sdp"].toString();
    QString localAnswer;
    if (!createMediaForOffer(*session, *getChannelById(channelId), remoteSdp, localAnswer)) {
        response["error"] = QString("Failed to handle WebRTC offer for session %1").arg(actualSessionId);
        return false;
    }
    response["sessionId"] = actualSessionId;
    response["status"] = "offer_processed";
    response["sdp"] = localAnswer;
    
    m_lib->m_Log->writeLog(3, QString("WebRTC offer processed for session %1").arg(actualSessionId));
    return true;
}

bool WebRTCChannels::createMediaForOffer(WebRTCSession& session, const s_webrtc_channel& channel, const QString& remoteSdp, QString& outLocalSdp)
{
    // Parse remote SDP enough to extract ICE ufrag/pwd and presence of DTLS fingerprint
    // We must return a minimally valid answer with fingerprint and ICE creds to avoid browser errors
    QString remoteUfrag;
    QString remotePwd;
    QString remoteFingerprint;
    QString remoteSetup;
    const QStringList lines = remoteSdp.split("\n", Qt::SkipEmptyParts);
    QString remoteDirection = ""; // sendrecv/recvonly/sendonly/inactive
    for (const QString& lRaw : lines) {
        const QString l = lRaw.trimmed();
        if (l.startsWith("a=ice-ufrag:")) remoteUfrag = l.mid(QString("a=ice-ufrag:").length());
        else if (l.startsWith("a=ice-pwd:")) remotePwd = l.mid(QString("a=ice-pwd:").length());
        else if (l.startsWith("a=fingerprint:")) remoteFingerprint = l.mid(QString("a=fingerprint:").length());
        else if (l.startsWith("a=setup:")) remoteSetup = l.mid(QString("a=setup:").length());
        else if (l == "a=sendrecv" || l == "a=recvonly" || l == "a=sendonly" || l == "a=inactive") remoteDirection = l.mid(2);
    }

    // Enforce channel mode vs remote offer direction
    if (channel.sendOnly) {
        if (!remoteDirection.isEmpty() && remoteDirection.compare("recvonly", Qt::CaseInsensitive) != 0) {
            m_lib->m_Log->writeLog(1, QString("Offer rejected: channel sendOnly requires remote recvonly (got %1)").arg(remoteDirection));
        return false;
        }
    } else {
        if (!remoteDirection.isEmpty() && remoteDirection.compare("sendrecv", Qt::CaseInsensitive) != 0) {
            m_lib->m_Log->writeLog(1, QString("Offer rejected: channel bidirectional requires remote sendrecv (got %1)").arg(remoteDirection));
            return false;
        }
    }

    // Store for trickle ICE processing
    session.remoteUfrag = remoteUfrag;
    session.remotePwd = remotePwd;

    // Initialize ICE transport and session (answerer role, controlled)
    if (!initializeIceTransport(session, channel, remoteUfrag, remotePwd)) {
        return false;
    }
    
    session.isActive = false;

    // Parse remote and local SDP into pjmedia_sdp_session and drive pjmedia ICE/SRTP transport
    if (!session.iceTransport) {
        return false;
    }
    
    QByteArray rba = remoteSdp.toUtf8();
    rba.append('\0');
    char *rbuf = rba.data();
    pjmedia_sdp_session *rem = nullptr;
    if (pjmedia_sdp_parse(m_webrtcPool, rbuf, (pj_size_t)strlen(rbuf), &rem) != PJ_SUCCESS || !rem) {
        return false;
    }
    session.remoteSdp = rem;

    // Wrap ICE with SRTP adapter first; media_create MUST be called on SRTP transport
    if (!session.srtpTransport) {
        if (!initializeDtlsSrtp(session)) {
            return false;
        }
    }
    // Initialize media transport for this session with remote SDP (prepares ICE/DTLS)
    pjmedia_transport *mt = session.srtpTransport ? session.srtpTransport : session.iceTransport;
    pjmedia_transport_media_create(mt, m_webrtcPool, PJMEDIA_TPMED_RTCP_MUX, rem, 0);

    // Build base SDP then add an audio media line (similar to pjwebrtc approach)
    pjmedia_transport_info tinfo; pjmedia_transport_info_init(&tinfo);
    pjmedia_transport_get_info(mt, &tinfo);

    // Create base SDP with origin 0.0.0.0
    pjmedia_sdp_session *loc = nullptr;
    pj_sockaddr origin; pj_bzero(&origin, sizeof(origin));
    pj_str_t originStr = pj_str(const_cast<char*>("0.0.0.0"));
    pj_sockaddr_parse(pj_AF_INET(), 0, &originStr, &origin);
    if (pjmedia_endpt_create_base_sdp(pjsua_get_pjmedia_endpt(), m_webrtcPool, nullptr, &origin, &loc) != PJ_SUCCESS || !loc) {
        return false;
    }

    // Force RTP/RTCP default addresses to 0.0.0.0:9
    pj_sockaddr zero; pj_bzero(&zero, sizeof(zero));
    pj_str_t zeroStr = pj_str(const_cast<char*>("0.0.0.0:9"));
    pj_sockaddr_parse(pj_AF_INET(), 0, &zeroStr, &zero);
    tinfo.sock_info.rtp_addr_name = zero;
    tinfo.sock_info.rtcp_addr_name = zero;

    // Create audio m-line from endpoint capabilities
    pjmedia_sdp_media *m = nullptr;
    pjmedia_endpt_create_sdp_param sdp_opt; pjmedia_endpt_create_sdp_param_default(&sdp_opt);
    if (pjmedia_endpt_create_audio_sdp(pjsua_get_pjmedia_endpt(), m_webrtcPool, &tinfo.sock_info, &sdp_opt, &m) != PJ_SUCCESS || !m) {
        return false;
    }

    // Set m-line port to 9 and minimal attrs; let transport encode DTLS/ICE details
    m->desc.port = 9;
    pj_str_t midVal   = pj_strdup3(m_webrtcPool, "0");
    pjmedia_sdp_attr *a_mid    = pjmedia_sdp_attr_create(m_webrtcPool, "mid", &midVal);
    pjmedia_sdp_attr *a_mux    = pjmedia_sdp_attr_create(m_webrtcPool, "rtcp-mux", nullptr);
    pjmedia_sdp_attr *a_rsize  = pjmedia_sdp_attr_create(m_webrtcPool, "rtcp-rsize", nullptr);
    // Direction: if channel is audio-only (send-only), advertise sendonly
    pjmedia_sdp_attr *a_sendrecv = nullptr;
    if (channel.sendOnly) {
        a_sendrecv = pjmedia_sdp_attr_create(m_webrtcPool, "sendonly", nullptr);
    } else {
        a_sendrecv = pjmedia_sdp_attr_create(m_webrtcPool, "sendrecv", nullptr);
    }
    pjmedia_sdp_media_add_attr(m, a_mid);
    pjmedia_sdp_media_add_attr(m, a_mux);
    pjmedia_sdp_media_add_attr(m, a_rsize);
    pjmedia_sdp_media_add_attr(m, a_sendrecv);

    // Append media to session
    loc->media[loc->media_count++] = m;

    // Session-level bundle and trickle indication
    pj_str_t bundleVal = pj_strdup3(m_webrtcPool, "BUNDLE 0");
    pjmedia_sdp_attr *a_group = pjmedia_sdp_attr_create(m_webrtcPool, "group", &bundleVal);
    pj_str_t trickleVal = pj_strdup3(m_webrtcPool, "trickle");
    pjmedia_sdp_attr *a_trickle = pjmedia_sdp_attr_create(m_webrtcPool, "ice-options", &trickleVal);
    pjmedia_sdp_attr_add(&loc->attr_count, loc->attr, a_group);
    pjmedia_sdp_attr_add(&loc->attr_count, loc->attr, a_trickle);

    // Encode transport-specific attributes (ICE + DTLS-SRTP) into local SDP
    pj_status_t enc_st = pjmedia_transport_encode_sdp(mt, m_webrtcPool, loc, rem, 0);
    if (enc_st != PJ_SUCCESS) {
        m_lib->m_Log->writeLog(1, QString("encode_sdp failed %1").arg(enc_st));
        return false;
    }
    // encode_sdp succeeded

    // Force DTLS role to active (we act as DTLS client) to initiate handshake from our side
    auto force_setup_active = [this](pjmedia_sdp_attr **attrs, unsigned &count){
        // Remove existing setup attributes and add passive
        unsigned w = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (pj_stricmp2(&attrs[i]->name, "setup") == 0) continue;
            attrs[w++] = attrs[i];
        }
        count = w;
        pj_str_t active = pj_strdup3(m_webrtcPool, "active");
        pjmedia_sdp_attr *a_setup = pjmedia_sdp_attr_create(m_webrtcPool, "setup", &active);
        attrs[count++] = a_setup;
    };
    // Apply at session-level and media-level only if remote is actpass
    if (remoteSetup.compare("actpass", Qt::CaseInsensitive) == 0) {
        force_setup_active(loc->attr, loc->attr_count);
        if (loc->media_count > 0 && loc->media[0]) {
            force_setup_active(loc->media[0]->attr, loc->media[0]->attr_count);
        }
        m_lib->m_Log->writeLog(3, "DTLS setup override applied: active (remote actpass)");
    } else {
        m_lib->m_Log->writeLog(3, QString("DTLS setup not overridden (remote setup=%1)").arg(remoteSetup.isEmpty()?"-":remoteSetup));
    }

    // Negotiate SDP using pjmedia's negotiator to align payload types and attributes
    pj_status_t neg_st;
    if (!session.sdpNeg) {
        neg_st = pjmedia_sdp_neg_create_w_remote_offer(m_webrtcPool, loc, rem, &session.sdpNeg);
        if (neg_st != PJ_SUCCESS) {
            m_lib->m_Log->writeLog(1, QString("sdp_neg_create_w_remote_offer failed %1").arg(neg_st));
            return false;
        }
    }
    neg_st = pjmedia_sdp_neg_set_local_answer(m_webrtcPool, session.sdpNeg, loc);
    if (neg_st != PJ_SUCCESS) {
        m_lib->m_Log->writeLog(1, QString("sdp_neg_set_local_answer failed %1").arg(neg_st));
        return false;
    }
    // Finalize negotiation to produce active SDP
    neg_st = pjmedia_sdp_neg_negotiate(m_webrtcPool, session.sdpNeg, PJ_TRUE);
    if (neg_st != PJ_SUCCESS) {
        m_lib->m_Log->writeLog(1, QString("sdp_neg_negotiate failed %1").arg(neg_st));
        return false;
    }

    // Get active negotiated SDPs
    const pjmedia_sdp_session *neg_loc = nullptr;
    const pjmedia_sdp_session *neg_rem = nullptr;
    neg_st = pjmedia_sdp_neg_get_active_local(session.sdpNeg, &neg_loc);
    if (neg_st != PJ_SUCCESS || !neg_loc) {
        m_lib->m_Log->writeLog(1, "sdp_neg_get_active_local failed");
        return false;
    }
    neg_st = pjmedia_sdp_neg_get_active_remote(session.sdpNeg, &neg_rem);
    if (neg_st != PJ_SUCCESS || !neg_rem) {
        m_lib->m_Log->writeLog(1, "sdp_neg_get_active_remote failed");
        return false;
    }

    // Append currently known local candidates into the SDP; avoid signaling end-of-candidates now
    pj_bool_t end_flag = PJ_FALSE;
    if (session.iceTransport) {
        pjmedia_ice_trickle_send_local_cand(session.iceTransport, m_webrtcPool, loc, &end_flag);
    }
    if (loc->media_count > 0 && loc->media[0]) {
        pjmedia_sdp_media *m0 = loc->media[0];
        // Strip any premature end-of-candidates from initial answer
        unsigned w = 0;
        for (unsigned i = 0; i < m0->attr_count; ++i) {
            if (pj_stricmp2(&m0->attr[i]->name, "end-of-candidates") == 0) {
                continue; // skip
            }
            m0->attr[w++] = m0->attr[i];
        }
        m0->attr_count = w;
    }

    // Validate DTLS-SRTP presence; if missing, abort with clear error
    if (!(neg_loc->media_count > 0 && neg_loc->media[0])) {
        m_lib->m_Log->writeLog(1, "SDP error: no media in local SDP");
        return false;
    }
    pjmedia_sdp_media *m0 = neg_loc->media[0];
    bool has_fp = false;
    QString localSetup = "";
    for (unsigned i = 0; i < m0->attr_count; ++i) {
        if (pj_stricmp2(&m0->attr[i]->name, "fingerprint") == 0) { has_fp = true; }
        if (pj_stricmp2(&m0->attr[i]->name, "setup") == 0) {
            localSetup = QString::fromUtf8(pj_strbuf(&m0->attr[i]->value), pj_strlen(&m0->attr[i]->value));
        }
    }
    if (!has_fp) {
        m_lib->m_Log->writeLog(1, "SDP error: DTLS fingerprint missing in local SDP. Check pjproject build for DTLS.");
        return false;
    }
    m_lib->m_Log->writeLog(3, QString("DTLS setup: %1 (remote=%2)")
                               .arg(localSetup.isEmpty()?"-":localSetup)
                               .arg(remoteSetup.isEmpty()?"-":remoteSetup));

    // Print local SDP to send to browser (should now include opus offerings)
    char printBuf[4096];
    int printed = pjmedia_sdp_print(neg_loc, printBuf, sizeof(printBuf));
    if (printed <= 0) {
        return false;
    }
    outLocalSdp = QString::fromUtf8(printBuf, printed);

    session.localSdp = const_cast<pjmedia_sdp_session*>(neg_loc);
    session.remoteSdp = const_cast<pjmedia_sdp_session*>(neg_rem);

    // Start media transport using negotiated SDPs (enables trickle + starts DTLS-SRTP)
    pjmedia_transport_media_start(mt, m_webrtcPool,
                                  const_cast<pjmedia_sdp_session*>(neg_loc),
                                  const_cast<pjmedia_sdp_session*>(neg_rem), 0);
    // Diagnostics: log selected local RTP/RTCP address names
    {
        pjmedia_transport_info tdi; pjmedia_transport_info_init(&tdi);
        pjmedia_transport_get_info(mt, &tdi);
        char lrtp[128] = {0}, lrtcp[128] = {0};
        pj_sockaddr_print(&tdi.sock_info.rtp_addr_name, lrtp, sizeof(lrtp), 3);
        pj_sockaddr_print(&tdi.sock_info.rtcp_addr_name, lrtcp, sizeof(lrtcp), 3);
    // Media transport started (RTP/RTCP): %1 / %2
    }
    session.iceSessionReady = true;

    // Drain any queued ICE candidates now that trickle is enabled
    processStoredIceCandidates(session.sessionId);

    // Defer media stream creation until SRTP/DTLS negotiation completes
    // createOpusStream(session); // moved to onSrtpNegoCompleteCb

    // If the DTLS role is active, we proactively trigger media transport start already above.
    // Some stacks need an explicit start to kick off DTLS handshake; we already did that.

    return true;
}

bool WebRTCChannels::initializeIceTransport(WebRTCSession& session, const s_webrtc_channel& channel,
                                            const QString& remoteUfrag, const QString& remotePwd)
{
    Q_UNUSED(channel);
    if (session.iceTransport) return true;

    pjmedia_endpt *med_ep = pjsua_get_pjmedia_endpt();
    if (!med_ep) {
        m_lib->m_Log->writeLog(1, "ICE: pjmedia endpoint not available");
        return false;
    }

    pj_ice_strans_cfg ice_cfg; pj_bzero(&ice_cfg, sizeof(ice_cfg));
    pj_ice_strans_cfg_default(&ice_cfg);
    // Initialize STUN config from pjmedia endpoint
    // Supply required STUN cfg from our own pjlib objects
    if (!m_webrtcIoqueue || !m_webrtcTimer) {
        m_lib->m_Log->writeLog(1, "ICE: missing internal ioqueue/timer");
        return false;
    }
    pj_stun_config_init(&ice_cfg.stun_cfg, &m_webrtcCp.factory, 0, m_webrtcIoqueue, m_webrtcTimer);
    // Address family & trickle
    ice_cfg.af = pj_AF_INET();
    // Enable trickle ICE (half mode for interop)
    ice_cfg.opt.trickle = PJ_ICE_SESS_TRICKLE_HALF;
    // Configure STUN server from channel settings
    if (!channel.stunServer.isEmpty()) {
        QString stun = channel.stunServer;
        if (stun.startsWith("stun:")) stun = stun.mid(5);
        QString host = stun;
        quint16 port = PJ_STUN_PORT;
        int colon = stun.indexOf(":");
        if (colon > 0) {
            host = stun.left(colon);
            bool ok=false; quint16 p = static_cast<quint16>(stun.mid(colon+1).toUShort(&ok));
            if (ok) port = p;
        }
        // Resolve host to IPv4 string since we don't attach a DNS resolver to STUN config
        pj_addrinfo ai[4];
        unsigned cnt = PJ_ARRAY_SIZE(ai);
        pj_str_t host_str = pj_strdup3(m_webrtcPool, host.toUtf8().constData());
        pj_status_t gaist = pj_getaddrinfo(pj_AF_INET(), &host_str, &cnt, ai);
        QString ipStr = host;
        if (gaist == PJ_SUCCESS && cnt > 0) {
            char buf[PJ_INET6_ADDRSTRLEN+4];
            pj_sockaddr_print(&ai[0].ai_addr, buf, sizeof(buf), 0);
            ipStr = QString::fromUtf8(buf);
            int cpos = ipStr.lastIndexOf(':');
            if (cpos > 0) ipStr = ipStr.left(cpos);
            m_lib->m_Log->writeLog(3, QString("ICE: Resolved STUN host %1 -> %2").arg(host, ipStr));
        } else {
            m_lib->m_Log->writeLog(2, QString("ICE: Failed to resolve STUN host %1, using literal").arg(host));
        }
        pj_str_t srv = pj_strdup3(m_webrtcPool, ipStr.toUtf8().constData());
        ice_cfg.stun.server = srv;
        ice_cfg.stun.port = port;
        ice_cfg.stun.cfg.ka_interval = 300; // seconds
        m_lib->m_Log->writeLog(3, QString("ICE: STUN server %1:%2").arg(ipStr).arg(port));
    }

    pjmedia_transport *tp = nullptr;
    // Create ICE transport; use callbacks so we get on_ice_complete and new candidates
    pjmedia_ice_cb ice_cb; pj_bzero(&ice_cb, sizeof(ice_cb));
    ice_cb.on_ice_complete = &WebRTCChannels::onIceCompleteCb;
    ice_cb.on_ice_complete2 = &WebRTCChannels::onIceCompleteCb2;
    ice_cb.on_new_candidate = &WebRTCChannels::onIceNewCandidateCb;
    pj_status_t status = pjmedia_ice_create(med_ep, "webrtc-ice", 1, &ice_cfg, &ice_cb, &tp);
    if (status != PJ_SUCCESS || !tp) {
        m_lib->m_Log->writeLog(1, QString("ICE: create failed %1").arg(status));
        return false;
    }

    // Store remote creds; we'll apply when ICE session is initialized

    session.iceTransport = tp;
    m_lib->m_Log->writeLog(3, QString("ICE: transport initialized for session %1").arg(session.sessionId));
    return true;
}

bool WebRTCChannels::addRemoteIceCandidate(WebRTCSession& session, const QString& candidate, int mlineIndex, const QString& mid)
{
    Q_UNUSED(mlineIndex);
    Q_UNUSED(mid);
    // Parse the candidate and apply via pjmedia_ice_trickle_update when possible
    if (!session.iceTransport || !session.iceSessionReady) {
        session.pendingIceCandidates.append(candidate);
        m_lib->m_Log->writeLog(3, QString("Queued ICE candidate for session %1").arg(session.sessionId));
        return true;
    }

    pj_ice_sess_cand rcand; pj_bzero(&rcand, sizeof(rcand));
    if (!parseRemoteCandidate(candidate, rcand)) {
        // Fallback to queue
        session.pendingIceCandidates.append(candidate);
        m_lib->m_Log->writeLog(2, QString("Failed to parse ICE candidate; queued for session %1").arg(session.sessionId));
        return true;
    }
    pj_str_t rufrag = pj_str(const_cast<char*>(session.remoteUfrag.toUtf8().data()));
    pj_str_t rpwd   = pj_str(const_cast<char*>(session.remotePwd.toUtf8().data()));
    pj_status_t st = pjmedia_ice_trickle_update(session.iceTransport, &rufrag, &rpwd, 1, &rcand, PJ_FALSE);
    if (st != PJ_SUCCESS) {
        session.pendingIceCandidates.append(candidate);
        m_lib->m_Log->writeLog(2, QString("pjmedia_ice_trickle_update defer (%1); queued for %2").arg(st).arg(session.sessionId));
        return true;
    }
    m_lib->m_Log->writeLog(3, QString("Applied ICE candidate via trickle for session %1").arg(session.sessionId));
    return true;
}

bool WebRTCChannels::parseRemoteCandidate(const QString& candStr, pj_ice_sess_cand& outCand)
{
    // Expected: candidate:<id> <comp> <proto> <prio> <addr> <port> typ <type> ...
    QStringList parts = candStr.split(' ');
    if (parts.size() < 8) return false;
    // Basic fields
    // comp_id is parts[1]
    bool ok = false;
    int comp = parts[1].toInt(&ok);
    if (!ok || comp < 1) comp = 1;
    outCand.comp_id = static_cast<pj_uint8_t>(comp);
    // priority
    quint32 prio = parts[3].toUInt(&ok);
    if (ok) outCand.prio = prio;
    // foundation from first token after "candidate:"
    QString f0 = parts[0];
    int colon = f0.indexOf(":");
    if (colon > 0 && colon+1 < f0.size()) {
        QString foundation = f0.mid(colon+1);
        pj_str_t fstr = pj_strdup3(m_webrtcPool, foundation.toUtf8().constData());
        outCand.foundation = fstr;
    }
    // address and port
    QString addr = parts[4];
    int port = parts[5].toInt(&ok);
    if (!ok) return false;
    pj_bzero(&outCand.addr, sizeof(outCand.addr));

    auto resolveAndFill = [&](const QString& host)->bool {
        // Try numeric first
        pj_str_t ipstr = pj_str(const_cast<char*>(host.toUtf8().data()));
        if (host.contains(":")) {
            // IPv6
            pj_sockaddr_in6 *sin6 = &outCand.addr.ipv6;
            sin6->sin6_family = pj_AF_INET6();
            sin6->sin6_port = pj_htons(static_cast<pj_uint16_t>(port));
            if (pj_sockaddr_parse(pj_AF_INET6(), 0, &ipstr, &outCand.addr) == PJ_SUCCESS) return true;
        } else {
            // IPv4
            pj_sockaddr_in *sin = &outCand.addr.ipv4;
            sin->sin_family = pj_AF_INET();
            sin->sin_port = pj_htons(static_cast<pj_uint16_t>(port));
            pj_in_addr ip4; pj_bzero(&ip4, sizeof(ip4));
            if (pj_inet_aton(&ipstr, &ip4) == PJ_SUCCESS) { sin->sin_addr = ip4; return true; }
        }
        // Not numeric: resolve via DNS/mDNS
        pj_addrinfo ai[4]; unsigned cnt = PJ_ARRAY_SIZE(ai);
        pj_str_t h = pj_strdup3(m_webrtcPool, host.toUtf8().constData());
        if (pj_getaddrinfo(pj_AF_UNSPEC(), &h, &cnt, ai) != PJ_SUCCESS || cnt == 0) return false;
        // Prefer IPv4 if available
        pj_sockaddr selected = ai[0].ai_addr;
        for (unsigned i=0;i<cnt;i++){ if (ai[i].ai_addr.addr.sa_family==pj_AF_INET()) { selected=ai[i].ai_addr; break; } }
        // Set port
        if (selected.addr.sa_family==pj_AF_INET6()) {
            selected.ipv6.sin6_port = pj_htons(static_cast<pj_uint16_t>(port));
        } else {
            selected.ipv4.sin_port = pj_htons(static_cast<pj_uint16_t>(port));
        }
        outCand.addr = selected;
        return true;
    };

    if (!resolveAndFill(addr)) return false;
    // type
    int typIdx = parts.indexOf("typ");
    if (typIdx != -1 && typIdx + 1 < parts.size()) {
        QString t = parts[typIdx + 1];
        if (t == "host") outCand.type = PJ_ICE_CAND_TYPE_HOST;
        else if (t == "srflx") outCand.type = PJ_ICE_CAND_TYPE_SRFLX;
        else if (t == "relay") outCand.type = PJ_ICE_CAND_TYPE_RELAYED;
        else outCand.type = PJ_ICE_CAND_TYPE_HOST;
    } else {
        outCand.type = PJ_ICE_CAND_TYPE_HOST;
    }
    // related address/port
    int raddrIdx = parts.indexOf("raddr");
    int rportIdx = parts.indexOf("rport");
    if (raddrIdx != -1 && raddrIdx + 1 < parts.size() && rportIdx != -1 && rportIdx + 1 < parts.size()) {
        QString raddr = parts[raddrIdx + 1];
        int rport = parts[rportIdx + 1].toInt(&ok);
        if (ok) {
            pj_str_t rip = pj_str(const_cast<char*>(raddr.toUtf8().data()));
            if (raddr.contains(":")) {
                pj_sockaddr_parse(pj_AF_INET6(), 0, &rip, &outCand.rel_addr);
                outCand.rel_addr.ipv6.sin6_port = pj_htons(static_cast<pj_uint16_t>(rport));
            } else {
                pj_sockaddr_parse(pj_AF_INET(), 0, &rip, &outCand.rel_addr);
                outCand.rel_addr.ipv4.sin_port = pj_htons(static_cast<pj_uint16_t>(rport));
            }
        }
    }
    return true;
}

bool WebRTCChannels::initializeDtlsSrtp(WebRTCSession& session)
{
    if (!session.iceTransport) return false;

    // Create SRTP wrapper over ICE transport
    pjmedia_srtp_setting srtp_set; pjmedia_srtp_setting_default(&srtp_set);
    // Align with pjwebrtc: enforce DTLS-SRTP keying and close member on destroy
    srtp_set.use = PJMEDIA_SRTP_MANDATORY;
    srtp_set.close_member_tp = PJ_TRUE;
    srtp_set.keying_count = 1;
    srtp_set.keying[0] = PJMEDIA_SRTP_KEYING_DTLS_SRTP;
    // Optional SDES second preference (kept unset to avoid SDP SDES attributes)
    // srtp_set.keying[1] = PJMEDIA_SRTP_KEYING_SDES;
    srtp_set.user_data = (void*)s_instance;
    srtp_set.cb.on_srtp_nego_complete = &WebRTCChannels::onSrtpNegoCompleteCb;

    pjmedia_transport *srtp_tp = nullptr;
    pj_status_t st = pjmedia_transport_srtp_create(pjsua_get_pjmedia_endpt(), session.iceTransport, &srtp_set, &srtp_tp);
    if (st != PJ_SUCCESS || !srtp_tp) {
        m_lib->m_Log->writeLog(1, QString("SRTP: create failed %1").arg(st));
        return false;
    }
    session.srtpTransport = srtp_tp;
    m_lib->m_Log->writeLog(3, QString("SRTP: transport created for session %1").arg(session.sessionId));
    return true;
}

bool WebRTCChannels::createOpusStream(WebRTCSession& session)
{
    if (!session.localSdp || !session.remoteSdp) return false;
    // Ensure ICE session is initialized and negotiation completed before creating media
    if (!session.iceTransport || !session.iceSessionReady) {
        m_lib->m_Log->writeLog(2, "Stream: deferring create until ICE session ready");
        return false;
    }
    
    pjmedia_stream_info si; pj_bzero(&si, sizeof(si));
    pj_status_t st = pjmedia_stream_info_from_sdp(&si, m_webrtcPool,
                                                  pjsua_get_pjmedia_endpt(),
                                                  session.localSdp,
                                                  session.remoteSdp,
                                                  0);
    if (st != PJ_SUCCESS) {
        m_lib->m_Log->writeLog(1, QString("Stream: info_from_sdp failed %1").arg(st));
            return false;
        }
    // Direction is set after we fetch channel settings below

    if (si.fmt.clock_rate == 0 || si.fmt.channel_cnt == 0) {
        // Fallback for cases where SDP parsing didn't populate format fully
        // Enforce Opus 48kHz stereo which matches our local SDP offer/answer
        si.type = PJMEDIA_TYPE_AUDIO;
        if (si.fmt.clock_rate == 0) si.fmt.clock_rate = 48000;
        if (si.fmt.channel_cnt == 0) si.fmt.channel_cnt = 2;
        pj_str_t opus = pj_strdup3(m_webrtcPool, "opus");
        si.fmt.encoding_name = opus;
        m_lib->m_Log->writeLog(2, QString("Stream: applied Opus fallback (rate=%1, ch=%2)")
                                   .arg(si.fmt.clock_rate).arg(si.fmt.channel_cnt));
        // Ensure remote addr families are sane to avoid pj_sockaddr_get_len assertion
        if (si.rem_addr.addr.sa_family != pj_AF_INET() && si.rem_addr.addr.sa_family != pj_AF_INET6()) {
            pj_sockaddr_in_init(&si.rem_addr.ipv4, 0, 0);
        }
        if (si.rem_rtcp.addr.sa_family != pj_AF_INET() && si.rem_rtcp.addr.sa_family != pj_AF_INET6()) {
            pj_sockaddr_in_init(&si.rem_rtcp.ipv4, 0, 0);
        }
    }

    if (si.param) {
        si.param->setting.vad = 0;
    }

    // Ensure socket address families are valid to avoid pj_sockaddr_get_len assertions
    auto ensure_addr_family = [](pj_sockaddr &sa){
        if (sa.addr.sa_family != pj_AF_INET() && sa.addr.sa_family != pj_AF_INET6()) {
            sa.addr.sa_family = pj_AF_INET();
            sa.ipv4.sin_family = pj_AF_INET();
            sa.ipv4.sin_port = 0;
            pj_bzero(&sa.ipv4.sin_addr, sizeof(sa.ipv4.sin_addr));
        }
    };
    ensure_addr_family(si.rem_addr);
    ensure_addr_family(si.rem_rtcp);

    // Fetch channel and adjust direction prior to stream creation
    s_webrtc_channel* channel = getChannelById(session.channelId);
    if (!channel) {
        m_lib->m_Log->writeLog(1, "Stream: channel not found");
        return false;
    }
    if (channel->sendOnly) {
        si.dir = PJMEDIA_DIR_ENCODING; // send only
    } else if (si.dir == 0) {
        si.dir = PJMEDIA_DIR_ENCODING_DECODING;
    }
    m_lib->m_Log->writeLog(3, QString("Stream info: dir=%1 clock=%2 ch=%3")
                               .arg((int)si.dir).arg((int)si.fmt.clock_rate).arg((int)si.fmt.channel_cnt));

    pjmedia_transport *mt = session.srtpTransport ? session.srtpTransport : session.iceTransport;
    st = pjmedia_stream_create(pjsua_get_pjmedia_endpt(), m_webrtcPool, &si, mt, nullptr, &session.mediaStream);
    if (st != PJ_SUCCESS || !session.mediaStream) {
        m_lib->m_Log->writeLog(1, QString("Stream: create failed %1").arg(st));
        return false;
    }

    if (pjmedia_stream_get_port(session.mediaStream, &session.streamPort) != PJ_SUCCESS || !session.streamPort) {
        m_lib->m_Log->writeLog(1, "Stream: get_port failed");
        return false;
    }
    // Sanity: ensure port has valid format info before adding to conf to avoid assertion
    const pjmedia_port_info &pi = session.streamPort->info;
    if (PJMEDIA_PIA_SRATE(&pi) == 0 || PJMEDIA_PIA_CCNT(&pi) == 0) {
        m_lib->m_Log->writeLog(1, "Stream: invalid port format (clock_rate/channel_count is zero), deferring conf add");
        return false;
    }

    // Per-stream split/comb strategy: if stereo/multichannel, build a per-stream splitcomb
    const unsigned srate = PJMEDIA_PIA_SRATE(&pi);
    const unsigned chcnt = PJMEDIA_PIA_CCNT(&pi);
    const unsigned spf   = PJMEDIA_PIA_SPF(&pi);

    if (chcnt > 1) {
        // Create splitcomb matching the stream
        st = pjmedia_splitcomb_create(m_webrtcPool, srate, chcnt, spf, 16, 0, &session.perStreamSplitComb);
        if (st != PJ_SUCCESS || !session.perStreamSplitComb) {
            m_lib->m_Log->writeLog(1, QString("SplitComb: create failed %1").arg(st));
        return false;
    }
        // Master-port wiring based on direction
        if (channel->sendOnly) {
            // Send-only: splitcomb -> stream
            st = pjmedia_master_port_create(m_webrtcPool, session.perStreamSplitComb, session.streamPort, 0, &session.mp_split_to_stream);
            if (st != PJ_SUCCESS) {
                m_lib->m_Log->writeLog(1, QString("MasterPort: split->stream create failed %1").arg(st));
                return false;
            }
            pjmedia_master_port_start(session.mp_split_to_stream);
        } else {
            // Receive path: stream -> splitcomb
            st = pjmedia_master_port_create(m_webrtcPool, session.streamPort, session.perStreamSplitComb, 0, &session.mp_stream_to_split);
            if (st != PJ_SUCCESS) {
                m_lib->m_Log->writeLog(1, QString("MasterPort: stream->split create failed %1").arg(st));
                return false;
            }
            pjmedia_master_port_start(session.mp_stream_to_split);
        }

        // Create reverse-channel mono ports and add them to pjsua conf, label them for AudioRouter (WRTC:)
        session.perStreamMonoPorts.clear();
        session.perStreamConfSlots.clear();
        for (unsigned i = 0; i < chcnt; ++i) {
            pjmedia_port *monoPort = nullptr;
            // Increase reverse-channel buffering (lower 8-bits = number of buffers)
            st = pjmedia_splitcomb_create_rev_channel(m_webrtcPool, session.perStreamSplitComb, i, 32, &monoPort);
            if (st != PJ_SUCCESS || !monoPort) {
                m_lib->m_Log->writeLog(1, QString("SplitComb: create_rev_channel(%1) failed %2").arg(i).arg(st));
                continue;
            }
            // Name the port so AudioRouter lists it (WRTC:<channelId>-Ch:<n>-Sess:<short>)
            QString shortSess = session.sessionId.left(8);
            QString portName = QString("WRTC:%1-Sess:%2-Ch:%3").arg(session.channelId).arg(shortSess).arg(i+1);
            pj_strdup2(m_webrtcPool, &monoPort->info.name, portName.toUtf8().constData());
            session.perStreamMonoPorts.append(monoPort);
            pjsua_conf_port_id mslot = PJSUA_INVALID_ID;
            st = pjsua_conf_add_port(m_webrtcPool, monoPort, &mslot);
            if (st == PJ_SUCCESS) {
                session.perStreamConfSlots.append(mslot);
                if (!channel->sendOnly) {
                    // Receive-only case: connect mono to master playout for monitoring
                    pjsua_conf_port_info masterInfo;
                    if (pjsua_conf_get_port_info(0, &masterInfo) == PJ_SUCCESS) {
                        pjsua_conf_connect(mslot, masterInfo.slot_id);
                    }
                }
            }
        }
        // Do not add streamPort itself to conf when using splitcomb
        session.confSlot = -1;
    } else {
        // Mono stream: add streamPort directly to conf and connect to master port (receive playout)
        pjsua_conf_port_id slot;
        st = pjsua_conf_add_port(m_webrtcPool, session.streamPort, &slot);
        if (st != PJ_SUCCESS) {
            m_lib->m_Log->writeLog(1, QString("Conf: add_port failed %1").arg(st));
            return false;
        }
        session.confSlot = slot;
        if (!channel->sendOnly) {
            pjsua_conf_port_info masterInfo;
            if (pjsua_conf_get_port_info(0, &masterInfo) == PJ_SUCCESS) {
                pjsua_conf_connect(session.confSlot, masterInfo.slot_id);
            }
        }
    }
    m_lib->m_AudioRouter->conferenceBridgeChanged();
    
    // Start stream
    st = pjmedia_stream_start(session.mediaStream);
    if (st != PJ_SUCCESS) {
        m_lib->m_Log->writeLog(1, QString("Stream: start failed %1").arg(st));
        return false;
    }
    
    // Legacy path connect is no longer used for per-stream splitcomb; mono path already auto-connected above

    m_lib->m_Log->writeLog(3, QString("Stream: Opus stream created (%1ch) and connected to routing").arg(chcnt));
    return true;
}

// ICE callbacks (no-op scaffolding)
void WebRTCChannels::onIceCompleteCb(pjmedia_transport *tp, pj_ice_strans_op op, pj_status_t status)
{
    if (!s_instance) return;
    const char *opname = (op==PJ_ICE_STRANS_OP_INIT? "init" : (op==PJ_ICE_STRANS_OP_NEGOTIATION? "negotiation" : "unknown"));
    // ICE complete: %1 (%2)

    // If ICE init completed successfully, mark session as ready to accept candidates/streams
    WebRTCSession* session = s_instance->findSessionByTransport(tp);
    if (session && op == PJ_ICE_STRANS_OP_INIT && status == PJ_SUCCESS) {
        session->iceSessionReady = true;
        // Flush any queued remote candidates gathered before init completed
        s_instance->processStoredIceCandidates(session->sessionId);
    }
}

void WebRTCChannels::onIceCompleteCb2(pjmedia_transport *tp, pj_ice_strans_op op, pj_status_t status, void *user_data)
{
    Q_UNUSED(user_data);
    onIceCompleteCb(tp, op, status);
}

void WebRTCChannels::onIceNewCandidateCb(pjmedia_transport *tp, const pj_ice_sess_cand *cand, pj_bool_t last)
{
    if (!s_instance) return;
    // ICE new local candidate (last=%1)

    // Find session
    WebRTCSession* session = s_instance->findSessionByTransport(tp);
    if (!session) return;

    // Build candidate line: candidate:<foundation> <comp_id> udp <prio> <ip> <port> typ <type>
    char addr_buf[PJ_INET6_ADDRSTRLEN+4];
    pj_sockaddr_print(&cand->addr, addr_buf, sizeof(addr_buf), 0); // IP only
    QString ip = QString::fromUtf8(addr_buf);
    int port = pj_sockaddr_get_port(&cand->addr);

    const char* typeStr = "host";
    switch (cand->type) {
        case PJ_ICE_CAND_TYPE_SRFLX: typeStr = "srflx"; break;
        case PJ_ICE_CAND_TYPE_RELAYED: typeStr = "relay"; break;
        case PJ_ICE_CAND_TYPE_HOST: default: typeStr = "host"; break;
    }

    QString foundation = QString::fromUtf8(pj_strbuf(&cand->foundation), pj_strlen(&cand->foundation));
    if (foundation.isEmpty()) foundation = "0";

    QString candLine = QString("candidate:%1 %2 udp %3 %4 %5 typ %6")
        .arg(foundation)
        .arg(cand->comp_id)
        .arg(cand->prio)
        .arg(ip)
        .arg(port)
        .arg(typeStr);

    // Re-emit via Qt signal; Websocket will broadcast to browser
    emit s_instance->webrtcIceCandidate(session->channelId, session->sessionId, candLine, 0, "0");
}

WebRTCSession* WebRTCChannels::findSessionByTransport(pjmedia_transport* tp)
{
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it.value().iceTransport == tp || it.value().srtpTransport == tp) {
            return &m_sessions[it.key()];
        }
    }
    return nullptr;
}

void WebRTCChannels::onSrtpNegoCompleteCb(pjmedia_transport *tp, pj_status_t status)
{
    if (!s_instance) return;
    // Find session and mark SRTP ready
    WebRTCSession* session = s_instance->findSessionByTransport(tp);
    if (!session) return;
    session->srtpReady = (status == PJ_SUCCESS);
    s_instance->m_lib->m_Log->writeLog(3, QString("SRTP negotiation complete: status=%1 session=%2")
                                       .arg(status).arg(session->sessionId));
    if (session->srtpReady && session->mediaStream == nullptr) {
        // Now safe to create and start media stream
        if (!s_instance->createOpusStream(*session)) {
            s_instance->m_lib->m_Log->writeLog(1, QString("Failed to create media stream after SRTP ready for %1")
                                               .arg(session->sessionId));
        }
    }
}

int WebRTCChannels::webrtcWorkerThread(void* arg)
{
    WebRTCChannels* self = static_cast<WebRTCChannels*>(arg);
    if (!self) return 0;
    while (!self->m_webrtcThreadQuit) {
        // Poll timer heap; obtain next due timeout for ioqueue
        pj_time_val timeout = {0, 10};
        if (self->m_webrtcTimer) {
            pj_time_val next = {0, 0};
            pj_timer_heap_poll(self->m_webrtcTimer, &next);
            // choose smaller timeout
            if (PJ_TIME_VAL_GT(next, timeout)) {
                // keep current timeout
            } else {
                timeout = next;
            }
        }
        if (timeout.msec >= 1000) timeout.msec = 999;
        if (self->m_webrtcIoqueue) {
            pj_ioqueue_poll(self->m_webrtcIoqueue, &timeout);
        } else {
            pj_thread_sleep(PJ_TIME_VAL_MSEC(timeout));
        }
    }
    return 0;
}

bool WebRTCChannels::processWebRTCAnswer(const QString& sessionId, const QJsonObject& answer, QJsonObject& response)
{
    WebRTCSession* session = getSession(sessionId);
    if (!session) {
        response["error"] = QString("Session %1 not found").arg(sessionId);
        return false;
    }
    
    if (!answer.contains("sdp")) {
        response["error"] = "SDP is required in WebRTC answer";
        return false;
    }
    
    // TODO: apply remote answer if we acted as offerer
    response["status"] = "answer_processed";
    
    m_lib->m_Log->writeLog(3, QString("WebRTC answer processed for session %1").arg(sessionId));
    return true;
}

bool WebRTCChannels::processWebRTCIceCandidate(const QString& sessionId, const QString& candidate, 
                                              int sdpMLineIndex, const QString& sdpMid)
{
    addIceCandidate(sessionId, candidate, sdpMLineIndex, sdpMid);
    return true;
}

bool WebRTCChannels::disconnectWebRTCCall(const QString& sessionId)
{
    hangupCall(sessionId);
    removeSession(sessionId);
    return true;
}


// Legacy channel-level connect removed

void WebRTCChannels::disconnectStreamFromChannel(const QString& sessionId)
{
    WebRTCSession* session = getSession(sessionId);
    if (!session) {
        return;
    }
    // Stop and destroy master ports if used
    if (session->mp_stream_to_split) {
        pjmedia_master_port_stop(session->mp_stream_to_split);
        pjmedia_master_port_destroy(session->mp_stream_to_split, PJ_FALSE);
        session->mp_stream_to_split = nullptr;
    }
    if (session->mp_split_to_stream) {
        pjmedia_master_port_stop(session->mp_split_to_stream);
        pjmedia_master_port_destroy(session->mp_split_to_stream, PJ_FALSE);
        session->mp_split_to_stream = nullptr;
    }

    // Remove per-stream conf slots and ports
    for (int mslot : session->perStreamConfSlots) {
        if (mslot != PJSUA_INVALID_ID) {
            pjsua_conf_remove_port(mslot);
        }
    }
    session->perStreamConfSlots.clear();
    session->perStreamMonoPorts.clear();
    if (session->perStreamSplitComb) {
        // No explicit destroy API; keep pointer null and let pool lifetime handle cleanup
        session->perStreamSplitComb = nullptr;
    }

    if (session->confSlot == -1) {
        return;
    }
    
    s_webrtc_channel* channel = getChannelById(session->channelId);
    if (!channel) {
        return;
    }
    
    // Disconnect both directions if mono path was used
    pjsua_conf_disconnect(channel->splitterSlot, session->confSlot);
    pjsua_conf_disconnect(session->confSlot, channel->splitterSlot);

    m_lib->m_AudioRouter->conferenceBridgeChanged();
    
    m_lib->m_Log->writeLog(3, QString("WebRTC stream disconnected from channel bridge: session %1")
                           .arg(sessionId));
}

// Legacy channel-level create removed

// Legacy channel-level remove removed


// Helper methods
QString WebRTCChannels::generateSessionId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// Removed PJSUA helpers in non-SIP mode

void WebRTCChannels::getWebRTCStatus(const QString& channelOrSessionId, QJsonObject& response)
{
    // Identify channel (and optional session filter)
    QString channelId = channelOrSessionId;
    WebRTCSession* sessionById = getSession(channelOrSessionId);
    if (sessionById) channelId = sessionById->channelId;

    s_webrtc_channel* channel = getChannelById(channelId);
    if (!channel) { response["error"] = "Channel not found"; return; }

    QJsonObject data;
    // Channel block
    QJsonObject ch;
    ch["id"] = channel->id;
    ch["description"] = channel->description;
    ch["enabled"] = channel->enabled;
    ch["splitterSlot"] = channel->splitterSlot;
    ch["sendOnly"] = channel->sendOnly;
    data["channel"] = ch;

    // Streams block (all sessions on this channel, or only the requested session)
    QJsonArray streams;
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        WebRTCSession &sess = it.value();
        if (sess.channelId != channel->id) continue;
        if (sessionById && sess.sessionId != sessionById->sessionId) continue;

        QJsonObject s;
        s["sessionId"] = sess.sessionId;
        s["isActive"] = sess.isActive;
        s["confSlot"] = sess.confSlot;
        s["splitterSlot"] = sess.splitterSlot;
        s["iceReady"] = sess.iceSessionReady;
        s["srtpReady"] = sess.srtpReady;
        s["hasStream"] = (sess.mediaStream != nullptr);
        s["masterWired"] = (sess.mp_stream_to_split != nullptr);

        // Pending ICE candidates
        s["pendingIceCandidatesCount"] = sess.pendingIceCandidates.size();
        QJsonArray pend;
        for (const QString &c : sess.pendingIceCandidates) pend.append(c);
        s["pendingIceCandidates"] = pend;

        // Media details (from stream info or negotiated SDP)
        QJsonObject media;
        if (sess.mediaStream) {
            pjmedia_stream_info si; pj_bzero(&si, sizeof(si));
            if (pjmedia_stream_get_info(sess.mediaStream, &si) == PJ_SUCCESS) {
                QJsonObject codec;
                QString enc = QString::fromUtf8(pj_strbuf(&si.fmt.encoding_name), pj_strlen(&si.fmt.encoding_name));
                codec["name"] = enc.isEmpty() ? "" : enc;
                codec["clockRate"] = (int)si.fmt.clock_rate;
                codec["channels"] = (int)si.fmt.channel_cnt;
                codec["tx_pt"] = (int)si.tx_pt;
                codec["rx_pt"] = (int)si.rx_pt;
                media["codec"] = codec;
                media["direction"] = (int)si.dir;
                media["rtcpMux"] = (bool)si.rtcp_mux;
            }

            // RTCP statistics
            pjmedia_rtcp_stat rst; pj_bzero(&rst, sizeof(rst));
            if (pjmedia_stream_get_stat(sess.mediaStream, &rst) == PJ_SUCCESS) {
                QJsonObject rtcp;
                QJsonObject tx; tx["pkt"] = (int)rst.tx.pkt; tx["bytes"] = (int)rst.tx.bytes; tx["loss"] = (int)rst.tx.loss; tx["discard"] = (int)rst.tx.discard; tx["dup"] = (int)rst.tx.dup; tx["reorder"] = (int)rst.tx.reorder; rtcp["tx"] = tx;
                QJsonObject rx; rx["pkt"] = (int)rst.rx.pkt; rx["bytes"] = (int)rst.rx.bytes; rx["loss"] = (int)rst.rx.loss; rx["discard"] = (int)rst.rx.discard; rx["dup"] = (int)rst.rx.dup; rx["reorder"] = (int)rst.rx.reorder;
                // Jitter (rx)
                QJsonObject j; j["n"] = rst.rx.jitter.n; j["min"] = rst.rx.jitter.min; j["max"] = rst.rx.jitter.max; j["mean"] = rst.rx.jitter.mean; j["last"] = rst.rx.jitter.last;
                rx["jitterUsec"] = j;
                rtcp["rx"] = rx;
                // RTT
                QJsonObject rtt; rtt["n"] = rst.rtt.n; rtt["min"] = rst.rtt.min; rtt["max"] = rst.rtt.max; rtt["mean"] = rst.rtt.mean; rtt["last"] = rst.rtt.last;
                rtcp["rttUsec"] = rtt;
                media["rtcpStat"] = rtcp;
            }

            // Jitter buffer state
            pjmedia_jb_state jbs; pj_bzero(&jbs, sizeof(jbs));
            if (pjmedia_stream_get_stat_jbuf(sess.mediaStream, &jbs) == PJ_SUCCESS) {
                QJsonObject jb;
                jb["frame_size"] = (int)jbs.frame_size;
                jb["prefetch"] = (int)jbs.prefetch;
                jb["min_prefetch"] = (int)jbs.min_prefetch;
                jb["max_prefetch"] = (int)jbs.max_prefetch;
                jb["size"] = (int)jbs.size;
                jb["avg_delay_ms"] = (int)jbs.avg_delay;
                jb["min_delay_ms"] = (int)jbs.min_delay;
                jb["max_delay_ms"] = (int)jbs.max_delay;
                jb["dev_delay_ms"] = (int)jbs.dev_delay;
                jb["lost_frames"] = (int)jbs.lost;
                jb["discard_frames"] = (int)jbs.discard;
                jb["empty_events"] = (int)jbs.empty;
                media["jitterBuffer"] = jb;
        }
    } else {
            // No stream yet; expose negotiated SDP info if available
            if (sess.localSdp && sess.localSdp->media_count > 0 && sess.localSdp->media[0]) {
                pjmedia_sdp_media *m0 = sess.localSdp->media[0];
                // Try to find opus clock/ch from rtpmap
                QString codecName = ""; int clockRate = 0; int chCnt = 0;
                for (unsigned i=0;i<m0->attr_count;i++) {
                    if (pj_stricmp2(&m0->attr[i]->name, "rtpmap")==0) {
                        QString v = QString::fromUtf8(pj_strbuf(&m0->attr[i]->value), pj_strlen(&m0->attr[i]->value));
                        if (v.contains("opus/", Qt::CaseInsensitive)) {
                            codecName = "opus";
                            // format: "<pt> opus/48000/2"
                            int slash = v.indexOf("opus/");
                            if (slash>0) {
                                QString tail = v.mid(slash+5);
                                QStringList sp = tail.split('/');
                                if (sp.size()>=1) clockRate = sp[0].toInt();
                                if (sp.size()>=2) chCnt = sp[1].toInt();
                            }
                            break;
                        }
                    }
                }
                QJsonObject codec;
                codec["name"] = codecName;
                codec["clockRate"] = clockRate;
                codec["channels"] = chCnt;
                media["codec"] = codec;
            }
        }
        s["media"] = media;

        streams.append(s);
    }
    data["streams"] = streams;

    response["data"] = data;
    response["error"] = "OK";
}
