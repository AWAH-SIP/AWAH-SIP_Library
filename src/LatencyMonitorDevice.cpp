#include "../include/LatencyMonitorDevice.h"
#include "../include/awahsiplib.h"
#include "../include/log.h"

#include <algorithm>

LatencyMonitorDevice::LatencyMonitorDevice(AWAHSipLib *lib)
    : m_lib(lib)
{
}

LatencyMonitorDevice::~LatencyMonitorDevice()
{
    destroy();
}

bool LatencyMonitorDevice::create(int direction, const QString &nameLabel)
{
    Q_UNUSED(direction);
    if (!m_lib) return false;
    if (m_confSlot != PJSUA_INVALID_ID) return true;

    pjmedia_port *p = nullptr; LatencyMonitorState *st=nullptr;
    QString label = nameLabel.isEmpty() ? "LatencyMonitor" : nameLabel;

    pj_pool_t *pool = pjsua_pool_create("lm", 1024, 1024);
    if (!pool) return false;

    if (latencymon_port_create(pool, label, (void*)m_lib, &p, &st) != PJ_SUCCESS) {
        pj_pool_release(pool); // Leak-Fix
        return false;
    }
    m_port = p; m_state = st;
    // Register notification so we can log per requirements
    latencymon_set_notify(m_state, this, [](void *user, const LatencyMeasurement &m){
        LatencyMonitorDevice *self = static_cast<LatencyMonitorDevice*>(user);
        if (!self) return;
        self->maybeLogOnNewMeasurement(m);
    });
    // Do not add to conference here; AudioRouter will attach via splitcomb and add reverse channels
    m_confSlot = PJSUA_INVALID_ID;
    return true;
}

void LatencyMonitorDevice::destroy()
{
    if (m_confSlot != PJSUA_INVALID_ID) {
        pjsua_conf_remove_port(m_confSlot);
        m_confSlot = PJSUA_INVALID_ID;
    }
    if (m_port) {
        pjmedia_port_destroy(m_port);
        m_port = nullptr;
    }
    if (m_state) { latencymon_port_destroy(m_state); m_state = nullptr; }
}

void LatencyMonitorDevice::resetStats()
{
    if (m_state) latencymon_reset_stats(m_state);
    m_loggedFirstSuccessCh1 = false;
    m_loggedFirstSuccessCh2 = false;
    m_loggedFirstFailureCh1 = false;
    m_loggedFirstFailureCh2 = false;
    m_successSeenCh1 = false;
    m_successSeenCh2 = false;
    m_sinceLastSummary=0;
}

LatencyStatsSummary LatencyMonitorDevice::computeSummary(const QVector<LatencyMeasurement> &ms) const
{
    LatencyStatsSummary s; s.count = ms.size();
    if (ms.isEmpty()) return s;
    QVector<double> ch1, ch2, dlt; ch1.reserve((int)ms.size()); ch2.reserve((int)ms.size()); dlt.reserve((int)ms.size());
    QVector<double> sc1, sc2; sc1.reserve((int)ms.size()); sc2.reserve((int)ms.size());
    for (const auto &m : ms) {
        if (m.rttCh1Ms>=0) { ch1.append(m.rttCh1Ms); if (m.corrCh1>0) sc1.append(m.corrCh1); s.successCh1++; } else { s.missCh1++; }
        if (m.rttCh2Ms>=0) { ch2.append(m.rttCh2Ms); if (m.corrCh2>0) sc2.append(m.corrCh2); s.successCh2++; } else { s.missCh2++; }
        if (m.deltaMs>=0) dlt.append(m.deltaMs);
        if (m.rttCh1Ms>=0 && m.rttCh2Ms>=0) s.bothSuccess++;
        else if (m.rttCh1Ms>=0 && m.rttCh2Ms<0) s.onlyCh1++;
        else if (m.rttCh2Ms>=0 && m.rttCh1Ms<0) s.onlyCh2++;
    }
    auto calc = [](QVector<double> v){
        if (v.isEmpty()) return std::tuple<double,double,double,double>(0,0,0,0);
        std::sort(v.begin(), v.end());
        double min=v.first(), max=v.last();
        double sum=0; for (double x : v) sum+=x; double avg=sum/((double)qMax<qsizetype>(1, v.size()));
        double med = v.size()%2 ? v[v.size()/2] : 0.5*(v[v.size()/2 -1] + v[v.size()/2]);
        return std::make_tuple(min,max,avg,med);
    };
    auto [min1,max1,avg1,med1] = calc(ch1);
    auto [min2,max2,avg2,med2] = calc(ch2);
    auto [mind,maxd,avgd,medd] = calc(dlt);
    auto [mins1,maxs1,avgs1,meds1] = calc(sc1);
    auto [mins2,maxs2,avgs2,meds2] = calc(sc2);
    s.minCh1=min1; s.maxCh1=max1; s.avgCh1=avg1; s.medCh1=med1;
    s.minCh2=min2; s.maxCh2=max2; s.avgCh2=avg2; s.medCh2=med2;
    s.minDelta=mind; s.maxDelta=maxd; s.avgDelta=avgd; s.medDelta=medd;
    s.minScoreCh1=mins1; s.maxScoreCh1=maxs1; s.avgScoreCh1=avgs1; s.medScoreCh1=meds1;
    s.minScoreCh2=mins2; s.maxScoreCh2=maxs2; s.avgScoreCh2=avgs2; s.medScoreCh2=meds2;
    return s;
}

LatencyStatsSummary LatencyMonitorDevice::getSummaryStats()
{
    QVector<LatencyMeasurement> ms = getMeasurements();
    return computeSummary(ms);
}

QVector<LatencyMeasurement> LatencyMonitorDevice::getMeasurements()
{
    if (!m_state) return {};
    QVector<LatencyMeasurement> copy = latencymon_get_measurements(m_state);
    // Apply logging policy on newest only if any
    if (!copy.isEmpty()) {
        const LatencyMeasurement &last = copy.last();
        maybeLogOnNewMeasurement(last);
    }
    return copy;
}

void LatencyMonitorDevice::maybeLogOnNewMeasurement(const LatencyMeasurement &m)
{
    if (!m_lib) return;
    // Per-channel first success/failure
    if (m.rttCh1Ms>=0 && !m_loggedFirstSuccessCh1) {
        m_loggedFirstSuccessCh1 = true; m_successSeenCh1 = true;
        // Rearm failure logging for next failure wave after this new success
        m_loggedFirstFailureCh1 = false;
        m_lib->m_Log->writeLog(3, QString("LatencyMon first success ch1: %1ms (score=%2)")
                                     .arg(m.rttCh1Ms,0,'f',1).arg(m.corrCh1,0,'f',3));
    } else if (m.rttCh1Ms<0 && m_successSeenCh1 && !m_loggedFirstFailureCh1) {
        m_loggedFirstFailureCh1 = true;
        m_lib->m_Log->writeLog(2, QString("LatencyMon first failure after success ch1"));
        // Rearm success logging so next success after this failure is reported once
        m_loggedFirstSuccessCh1 = false;
    }
    if (m.rttCh2Ms>=0 && !m_loggedFirstSuccessCh2) {
        m_loggedFirstSuccessCh2 = true; m_successSeenCh2 = true;
        // Rearm failure logging for next failure wave after this new success
        m_loggedFirstFailureCh2 = false;
        m_lib->m_Log->writeLog(3, QString("LatencyMon first success ch2: %1ms (score=%2)")
                                     .arg(m.rttCh2Ms,0,'f',1).arg(m.corrCh2,0,'f',3));
    } else if (m.rttCh2Ms<0 && m_successSeenCh2 && !m_loggedFirstFailureCh2) {
        m_loggedFirstFailureCh2 = true;
        m_lib->m_Log->writeLog(2, QString("LatencyMon first failure after success ch2"));
        // Rearm success logging so next success after this failure is reported once
        m_loggedFirstSuccessCh2 = false;
    }
    // Summary every 10 measurements as long as any channel is measuring successfully over time
    m_sinceLastSummary++;
    if (m_sinceLastSummary >= 10 && (m_successSeenCh1 || m_successSeenCh2)) {
        m_sinceLastSummary = 0;
        auto ms = latencymon_get_measurements(m_state);
        auto s = computeSummary(ms);
        m_lib->m_Log->writeLog(3, QString(
            "LatencyMon summary: N=%1 \n"
            "ch1[min/avg/med/max]=%2/%3/%4/%5 ms "
            "score1[min/avg/med/max]=%6/%7/%8/%9 \n"
            "ch2[min/avg/med/max]=%10/%11/%12/%13 ms "
            "score2[min/avg/med/max]=%14/%15/%16/%17 \n"
            "delta[min/avg/med/max]=%18/%19/%20/%21 ms \n"
            "succ[ch1/ch2/both/only1/only2]=%22/%23/%24/%25/%26 miss[ch1/ch2]=%27/%28 \n"
            "last[ch1/ch2/delta]=%29/%30/%31 ms last_score[ch1/ch2]=%32/%33")
            .arg(s.count)
            // ch1
            .arg(s.minCh1,0,'f',1).arg(s.avgCh1,0,'f',1).arg(s.medCh1,0,'f',1).arg(s.maxCh1,0,'f',1)
            // score1
            .arg(s.minScoreCh1,0,'f',3).arg(s.avgScoreCh1,0,'f',3).arg(s.medScoreCh1,0,'f',3).arg(s.maxScoreCh1,0,'f',3)
            // ch2
            .arg(s.minCh2,0,'f',1).arg(s.avgCh2,0,'f',1).arg(s.medCh2,0,'f',1).arg(s.maxCh2,0,'f',1)
            // score2
            .arg(s.minScoreCh2,0,'f',3).arg(s.avgScoreCh2,0,'f',3).arg(s.medScoreCh2,0,'f',3).arg(s.maxScoreCh2,0,'f',3)
            // delta
            .arg(s.minDelta,0,'f',1).arg(s.avgDelta,0,'f',1).arg(s.medDelta,0,'f',1).arg(s.maxDelta,0,'f',1)
            // success/miss
            .arg(s.successCh1).arg(s.successCh2).arg(s.bothSuccess).arg(s.onlyCh1).arg(s.onlyCh2).arg(s.missCh1).arg(s.missCh2)
            // last values
            .arg(m.rttCh1Ms,0,'f',1).arg(m.rttCh2Ms,0,'f',1).arg(m.deltaMs,0,'f',1).arg(m.corrCh1,0,'f',3).arg(m.corrCh2,0,'f',3)
        );
    }
}


