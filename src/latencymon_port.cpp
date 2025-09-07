#include "../include/latencymon_port.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <QDateTime>

extern "C" {
#include <pjmedia/port.h>
#include <pjmedia/converter.h>
}

static const unsigned kSampleRate = 8000;           // 8 kHz
static const unsigned kChannels   = 2;              // stereo
static const unsigned kFrameMs    = 20;             // 20 ms frames
static const unsigned kSampPerFrm = (kSampleRate/1000) * kFrameMs; // 160 samples per channel
static const unsigned kBurstMs    = 200;            // 200 ms burst
static const unsigned kBurstLen   = (kSampleRate/1000) * kBurstMs; // 1600 samples per channel

static const float kTxLevelDb = -15.0f;             // -15 dBFS
static const float kFadeMs = 5.0f;                  // 5ms fade
static const int   kDefaultRepeatMs = 3000;         // every ~3s
static const int   kStatsWindowMs = 5*60*1000;      // 5 minutes
static const double kMinAcceptCorr = 0.7;           // minimum NCC for channel accept

// State machine states
enum CycleState { CS_IDLE=0, CS_TX_BURST, CS_SILENCE, CS_ANALYZE };

struct SlidingStats {
    QVector<LatencyMeasurement> buf;
    int logEveryN = 20;
    int repeatIntervalMs = kDefaultRepeatMs;
    bool hadSuccess = false;
    int measurementCount = 0;
    double minAcceptCorr = kMinAcceptCorr; // threshold for accepting measurement
};

struct LatencyMonitorState {
    pjmedia_port port;
    pj_pool_t *pool = nullptr;
    void *appCtx = nullptr; // optional logger

    // Pre-generated PN/MLS template (mono)
    pj_int16_t *pnTpl16 = nullptr; // kBurstLen samples
    float     *pnTplFlt = nullptr; // kBurstLen samples
    unsigned   pnLen = kBurstLen;
    SlidingStats stats;

    // Cycle state and scheduling
    CycleState state = CS_IDLE;
    unsigned long long sampleClock = 0;          // per-channel samples emitted since init
    unsigned stateRemain = 0;                    // samples remaining in current state (per-channel)
    unsigned txOffset = 0;                       // index into PN during TX

    // RX state buffers
    QVector<float> rxCh1;
    QVector<float> rxCh2;
    unsigned rxKeepMs = 3000; // keep last 3s to accommodate path delay
    unsigned rxKeepSamples = (kSampleRate * rxKeepMs) / 1000;

    // Template ready flag
    bool initialized = false;
    bool analyzingPending = false;     // trigger ANALYZE once
    // Optional notify callback
    void *notifyUser = nullptr;
    latencymon_notify_cb notifyCb = nullptr;
};
// Notify hook for device wrapper
static void lm_notify(LatencyMonitorState *st, const LatencyMeasurement &m)
{
    typedef void (*notify_t)(void*, const LatencyMeasurement&);
    // store in port_data2 fields (user, cb) via port.info.reserved if needed
    // Simpler: keep in state
    struct Notify {
        void *user=nullptr; notify_t cb=nullptr;
    };
}


static pj_status_t lm_on_destroy(pjmedia_port *p)
{
    if (!p) return PJ_SUCCESS;
    LatencyMonitorState *st = (LatencyMonitorState*)p->port_data.pdata;
    if (st) {
        st->rxCh1.clear();
        st->rxCh2.clear();
        st->initialized = false;
        st->pnTpl16 = nullptr;
        st->pnTplFlt = nullptr;
    }
    return PJ_SUCCESS;
}

// Build PN (LFSR), band-limit (HP+LP), fade, scale, and store as int16/float in pool
static void build_pn_template(LatencyMonitorState *st)
{
    QVector<float> tmp; tmp.resize((int)kBurstLen);
    quint32 lfsr = 0x1u;
    for (unsigned i=0;i<kBurstLen;i++) {
        quint32 bit = ((lfsr>>0) ^ (lfsr>>1)) & 1u;
        lfsr = (lfsr>>1) | (bit<<14);
        tmp[(int)i] = (lfsr & 1u) ? 1.0f : -1.0f;
    }
    float hp_a = std::exp(-2.0f * (float)M_PI * 300.0f / (float)kSampleRate);
    float lp_a = std::exp(-2.0f * (float)M_PI * 3400.0f / (float)kSampleRate);
    float hp_prev = 0.0f, x_prev = 0.0f;
    for (int i=0;i<tmp.size();++i) {
        float x = tmp[i];
        float hp = hp_a * hp_prev + hp_a * (x - x_prev);
        hp_prev = hp; x_prev = x;
        tmp[i] = hp;
    }
    float lp_prev = tmp[0];
    for (int i=1;i<tmp.size();++i) {
        float y = lp_a * lp_prev + (1.0f - lp_a) * tmp[i];
        tmp[i] = y; lp_prev = y;
    }
    int fadeSamples = (int)std::round(kSampleRate * (kFadeMs/1000.0f));
    for (int i=0;i<tmp.size();++i) {
        float g = 1.0f;
        if (i < fadeSamples) g = (float)i / (float)std::max(1, fadeSamples);
        else if (i > (tmp.size()-fadeSamples)) g = (float)(tmp.size()-i) / (float)std::max(1, fadeSamples);
        tmp[i] *= g;
    }
    float target = std::pow(10.0f, kTxLevelDb/20.0f);
    float peak = 0.0f; for (float v : tmp) peak = std::max(peak, std::fabs(v));
    float scale = (peak > 1e-6f) ? (target/peak) : target;
    st->pnTpl16 = (pj_int16_t*) pj_pool_alloc(st->pool, sizeof(pj_int16_t) * kBurstLen);
    st->pnTplFlt = (float*) pj_pool_alloc(st->pool, sizeof(float) * kBurstLen);
    for (unsigned i=0;i<kBurstLen;i++) {
        float v = tmp[(int)i] * scale;
        st->pnTplFlt[i] = v;
        int s = (int)std::lrintf(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f);
        st->pnTpl16[i] = (pj_int16_t)s;
    }
    st->pnLen = kBurstLen;
}

static pj_status_t lm_get_frame(pjmedia_port *p, pjmedia_frame *frame)
{
    LatencyMonitorState *st = (LatencyMonitorState*)p->port_data.pdata;
    if (!st || !frame) return PJ_SUCCESS;

    const unsigned srate = PJMEDIA_PIA_SRATE(&p->info);
    const unsigned cc    = PJMEDIA_PIA_CCNT(&p->info);
    const unsigned spf   = PJMEDIA_PIA_SPF(&p->info);   // total samples across all channels
    const unsigned perChanSpf = cc ? (spf/cc) : 0;
    const unsigned avgFsz = PJMEDIA_PIA_AVG_FSZ(&p->info);
    pj_int16_t *smp = (pj_int16_t*)frame->buf;
    frame->size = avgFsz;

    // Always zero buffer first
    pjmedia_zero_samples(smp, spf);

    if (!st->initialized) {
        build_pn_template(st);
        st->initialized = true;
        st->state = CS_IDLE;
        st->sampleClock = 0;
        st->stateRemain = (unsigned)((kSampleRate * 100) / 1000); // 100ms initial idle
        st->txOffset = 0;
    }

    // State machine: IDLE -> TX_BURST -> SILENCE -> ANALYZE -> IDLE
    switch (st->state) {
    case CS_IDLE:
        if (st->stateRemain <= perChanSpf) {
            st->state = CS_TX_BURST;
            st->txOffset = 0;
            st->stateRemain = st->pnLen;
            // start new capture
            st->rxCh1.clear(); st->rxCh2.clear();
        } else {
            st->stateRemain -= perChanSpf;
        }
        break;
    case CS_TX_BURST: {
        unsigned remaining = (st->pnLen > st->txOffset) ? (st->pnLen - st->txOffset) : 0;
        unsigned toEmit = std::min(perChanSpf, remaining);
        for (unsigned i=0;i<toEmit;i++) {
            pj_int16_t v = st->pnTpl16[st->txOffset + i];
            for (unsigned ch=0; ch<cc; ++ch) smp[cc*i + ch] = v;
        }
        st->txOffset += toEmit;
        if (st->txOffset >= st->pnLen) {
            unsigned repeatSamps = (unsigned)((unsigned long long)srate * (unsigned long long)st->stats.repeatIntervalMs / 1000ULL);
            unsigned silence = (repeatSamps > st->pnLen) ? (repeatSamps - st->pnLen) : 0u;
            st->state = CS_SILENCE;
            st->stateRemain = silence;
        }
        }
        break;
    case CS_SILENCE:
        if (st->stateRemain <= perChanSpf) {
            st->state = CS_ANALYZE;
            st->stateRemain = 0;
            st->analyzingPending = true;
        } else {
            st->stateRemain -= perChanSpf;
        }
        break;
    case CS_ANALYZE:
        // silence output, analysis will run in put_frame once
        break;
    }

    frame->size = avgFsz;
    frame->type = PJMEDIA_FRAME_TYPE_AUDIO;
    // Advance sample clock by one frame per channel (only once per generated frame)
    st->sampleClock += perChanSpf;

    // Keep simple; counters are large enough
    return PJ_SUCCESS;
}

static double dot_norm(const float *a, const float *b, int n)
{
    // Zero-mean normalized cross-correlation (NCC)
    if (n <= 0) return 0.0;
    double sumA=0.0, sumB=0.0;
    for (int i=0;i<n;i++){ sumA += a[i]; sumB += b[i]; }
    double meanA = sumA / (double)n;
    double meanB = sumB / (double)n;
    double num = 0.0, aa=0.0, bb=0.0;
    for (int i=0;i<n;i++){
        double av = a[i] - meanA;
        double bv = b[i] - meanB;
        num += av*bv; aa += av*av; bb += bv*bv;
    }
    double den = std::sqrt(std::max(1e-18, aa*bb));
    if (den <= 0.0) return 0.0;
    return num / den;
}

static int argmax_ncc(const QVector<float> &x, const QVector<float> &templ, double &outScore)
{
    // Search lag in range [0 .. x.size()-templ.size()]
    int maxLag = (int)x.size() - (int)templ.size();
    if (maxLag < 0) { outScore = 0.0; return -1; }
    int bestLag = -1; double bestScore = -2.0; // NCC in [-1,1]
    // Coarse step 2 for speed, then refine around winner
    int coarseStep = 1; // precise search; small windows so ok
    for (int lag=0; lag<=maxLag; lag+=coarseStep) {
        double s = dot_norm(x.constData()+lag, templ.constData(), templ.size());
        if (s > bestScore) { bestScore = s; bestLag = lag; }
    }
    // refine +/- 2 around best
    int rs = std::max(0, bestLag-2);
    int re = std::min(maxLag, bestLag+2);
    for (int lag=rs; lag<=re; ++lag) {
        double s = dot_norm(x.constData()+lag, templ.constData(), templ.size());
        if (s > bestScore) { bestScore = s; bestLag = lag; }
    }
    outScore = bestScore;
    return bestLag;
}

static pj_status_t lm_put_frame(pjmedia_port *p, pjmedia_frame *frame)
{
    LatencyMonitorState *st = (LatencyMonitorState*)p->port_data.pdata;
    if (!st || !frame) return PJ_SUCCESS;

    const unsigned cc = PJMEDIA_PIA_CCNT(&p->info);
    const pj_int16_t *smp = (const pj_int16_t*)frame->buf;
    unsigned totalSamples = frame->size / sizeof(pj_int16_t);
    if (cc == 0 || totalSamples < cc) return PJ_SUCCESS;
    unsigned frames = totalSamples / cc;

    // Record only during TX_BURST and SILENCE
    if (st->state == CS_TX_BURST || st->state == CS_SILENCE) {
        st->rxCh1.reserve(st->rxCh1.size() + frames);
        st->rxCh2.reserve(st->rxCh2.size() + frames);
        for (unsigned i=0;i<frames;i++) {
            float c1 = (float)smp[cc*i + 0] / 32768.0f;
            float c2 = (cc>1) ? (float)smp[cc*i + 1] / 32768.0f : c1;
            st->rxCh1.append(c1);
            st->rxCh2.append(c2);
        }
        int maxKeep = (int)st->rxKeepSamples;
        if (st->rxCh1.size() > maxKeep) st->rxCh1.erase(st->rxCh1.begin(), st->rxCh1.end() - maxKeep);
        if (st->rxCh2.size() > maxKeep) st->rxCh2.erase(st->rxCh2.begin(), st->rxCh2.end() - maxKeep);
    }

    if (!st->initialized) return PJ_SUCCESS;

    // One-shot detection: run once during ANALYZE
    if (st->state == CS_ANALYZE && st->analyzingPending) {
        const int minRttMs = 0; // allow near-zero loopback in conference
        const int maxRttMs = std::min((int)st->rxKeepMs, std::max((int)kBurstMs + 100, st->stats.repeatIntervalMs - 100));
        const int minSamp = (int)((long long)minRttMs * (long long)kSampleRate / 1000LL);
        const int maxSamp = (int)((long long)maxRttMs * (long long)kSampleRate / 1000LL);
        if ((int)st->rxCh1.size() < (minSamp + (int)st->pnLen)) return PJ_SUCCESS; // wait until enough RX accumulated for full template

        int endIx = std::min((int)st->rxCh1.size(), maxSamp);
        int startIx = std::min(minSamp, endIx);
        int searchLen = endIx - startIx;
        if (searchLen < (int)st->pnLen) return PJ_SUCCESS;
        // Normalize windows to match template scale (-15 dBFS)
        QVector<float> w1 = QVector<float>(st->rxCh1.constBegin()+startIx, st->rxCh1.constBegin()+startIx+searchLen);
        QVector<float> w2 = QVector<float>(st->rxCh2.constBegin()+startIx, st->rxCh2.constBegin()+startIx+searchLen);
        auto rms = [](const QVector<float> &v){ double s=0; for(float x:v) s += x*x; return std::sqrt(s / std::max<qsizetype>(1,v.size())); };
        QVector<float> tplView; tplView.resize((int)st->pnLen);
        for (unsigned i=0;i<st->pnLen;i++) tplView[(int)i] = st->pnTplFlt[i];
        double tplRms = rms(tplView);
        if (tplRms > 1e-9) {
            double g1 = (rms(w1) > 1e-9) ? (tplRms / rms(w1)) : 1.0; for (int i=0;i<w1.size();++i) w1[i] = (float)(w1[i]*g1);
            double g2 = (rms(w2) > 1e-9) ? (tplRms / rms(w2)) : 1.0; for (int i=0;i<w2.size();++i) w2[i] = (float)(w2[i]*g2);
        }
        auto meanAbs = [](const QVector<float> &v){ double s=0; for (float x: v) s += std::fabs(x); return s / std::max<qsizetype>(1, v.size()); };
        double e1 = meanAbs(w1), e2 = meanAbs(w2);
        if (e1 < 0.001 && e2 < 0.001) {
            // Record a failed measurement
            LatencyMeasurement m; m.tsMs=QDateTime::currentMSecsSinceEpoch();
            st->stats.buf.append(m);
            st->stats.measurementCount++;
            // Evict old
            quint64 cutoff = m.tsMs - kStatsWindowMs;
            while (!st->stats.buf.isEmpty() && st->stats.buf.first().tsMs < cutoff) st->stats.buf.removeFirst();
            if (st->notifyCb) st->notifyCb(st->notifyUser, m);
            // reset and stay in ANALYZE->IDLE path
            st->rxCh1.clear(); st->rxCh2.clear();
            st->analyzingPending = false;
            st->state = CS_IDLE;
            st->stateRemain = 0;
            return PJ_SUCCESS;
        }

        double score1=0.0, score2=0.0;
        int lag1 = argmax_ncc(w1, tplView, score1);
        int lag2 = argmax_ncc(w2, tplView, score2);
        LatencyMeasurement m; m.tsMs = QDateTime::currentMSecsSinceEpoch();
        // Per-channel acceptance based on threshold
        if (lag1>=0 && std::fabs(score1) >= st->stats.minAcceptCorr) {
            m.rttCh1Ms = (double)(startIx + lag1) * 1000.0 / (double)kSampleRate;
            m.corrCh1 = std::fabs(score1);
            m.successCh1 = true;
        } else {
            m.rttCh1Ms = -1; m.corrCh1 = std::fabs(score1);
        }
        if (lag2>=0 && std::fabs(score2) >= st->stats.minAcceptCorr) {
            m.rttCh2Ms = (double)(startIx + lag2) * 1000.0 / (double)kSampleRate;
            m.corrCh2 = std::fabs(score2);
            m.successCh2 = true;
        } else {
            m.rttCh2Ms = -1; m.corrCh2 = std::fabs(score2);
        }
        if (m.rttCh1Ms >= 0 && m.rttCh2Ms >= 0) m.deltaMs = std::fabs(m.rttCh1Ms - m.rttCh2Ms);
        else m.deltaMs = -1;
        st->stats.buf.append(m);
        st->stats.measurementCount++;
        if (m.rttCh1Ms>=0 || m.rttCh2Ms>=0) st->stats.hadSuccess = true;
        quint64 cutoff = m.tsMs - kStatsWindowMs;
        while (!st->stats.buf.isEmpty() && st->stats.buf.first().tsMs < cutoff) st->stats.buf.removeFirst();
        if (st->notifyCb) st->notifyCb(st->notifyUser, m);
        // Reset buffers and go idle for next cycle
        st->rxCh1.clear(); st->rxCh2.clear();
        st->analyzingPending = false;
        st->state = CS_IDLE;
        st->stateRemain = 0;
    }
    return PJ_SUCCESS;
}

pj_status_t latencymon_port_create(pj_pool_t *pool,
                                   const QString &nameLabel,
                                   void *appCtx,
                                   pjmedia_port **p_port,
                                   LatencyMonitorState **p_state)
{
    if (!pool || !p_port || !p_state) return PJ_EINVAL;
    LatencyMonitorState *st = (LatencyMonitorState*) pj_pool_zalloc(pool, sizeof(LatencyMonitorState));
    if (!st) return PJ_ENOMEM;
    st->pool = pool;
    st->appCtx = appCtx;
    st->rxCh1.reserve((int)kSampleRate); st->rxCh2.reserve((int)kSampleRate);
    // Explicitly initialize fields that default member initializers won't set due to zalloc
    st->stats.logEveryN = 20;
    st->stats.repeatIntervalMs = kDefaultRepeatMs;
    st->stats.hadSuccess = false;
    st->stats.measurementCount = 0;
    st->stats.minAcceptCorr = kMinAcceptCorr; // ensure non-zero default after zalloc
    st->rxKeepMs = 3000;
    st->rxKeepSamples = (kSampleRate * st->rxKeepMs) / 1000;
    // Pre-generate PN/MLS now and set initial state
    build_pn_template(st);
    st->initialized = true;
    st->state = CS_IDLE;
    st->stateRemain = (unsigned)((kSampleRate * 100) / 1000);

    pj_bzero(&st->port, sizeof(st->port));
    pj_str_t nm; pj_cstr(&nm, nameLabel.toUtf8().constData());
    // signature 0 (user-defined). samples_per_frame is total per frame across channels (320 for 20ms @ 8kHz stereo)
    pjmedia_port_info_init(&st->port.info, &nm, 0,
                           kSampleRate, kChannels, 16, kSampPerFrm * kChannels);
    st->port.port_data.pdata = st;
    st->port.get_frame = &lm_get_frame;
    st->port.put_frame = &lm_put_frame;
    st->port.on_destroy = &lm_on_destroy;

    *p_port = &st->port;
    *p_state = st;
    return PJ_SUCCESS;
}

void latencymon_port_destroy(LatencyMonitorState *st)
{
    if (!st) return;
    st->rxCh1.clear(); st->rxCh2.clear(); st->stats.buf.clear();
    // Release owning pool last
    pj_pool_t *pool = st->pool;
    st->pool = nullptr;
    if (pool) pj_pool_release(pool);
}

void latencymon_reset_stats(LatencyMonitorState *st)
{
    if (!st) return; st->stats.buf.clear(); st->stats.hadSuccess=false; st->stats.measurementCount=0;
}

QVector<LatencyMeasurement> latencymon_get_measurements(LatencyMonitorState *st)
{
    if (!st) return {};
    // Drop old entries first
    quint64 now = QDateTime::currentMSecsSinceEpoch();
    quint64 cutoff = now - kStatsWindowMs;
    while (!st->stats.buf.isEmpty() && st->stats.buf.first().tsMs < cutoff) st->stats.buf.removeFirst();
    return st->stats.buf;
}

void latencymon_set_log_interval(LatencyMonitorState *st, int everyNMeasurements)
{
    if (!st) return; if (everyNMeasurements<1) everyNMeasurements=1; st->stats.logEveryN = everyNMeasurements;
}

void latencymon_set_repeat_interval_ms(LatencyMonitorState *st, int intervalMs)
{
    if (!st) return; if (intervalMs<500) intervalMs=500; st->stats.repeatIntervalMs = intervalMs;
}

void latencymon_set_min_corr(LatencyMonitorState *st, double minCorr)
{
    if (!st) return; if (minCorr < 0.0) minCorr = 0.0; if (minCorr > 1.0) minCorr = 1.0; st->stats.minAcceptCorr = minCorr;
}

void latencymon_set_notify(LatencyMonitorState *st, void *user, latencymon_notify_cb cb)
{
    if (!st) return; st->notifyUser = user; st->notifyCb = cb;
}



