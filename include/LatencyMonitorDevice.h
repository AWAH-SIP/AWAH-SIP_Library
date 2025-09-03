#ifndef LATENCY_MONITOR_DEVICE_H
#define LATENCY_MONITOR_DEVICE_H

#include <QObject>
#include <QVector>
#include <QString>

#include "latencymon_port.h"

extern "C" {
#include <pjsua-lib/pjsua.h>
}

class AWAHSipLib;

struct LatencyStatsSummary {
    // Per channel and delta
    double minCh1=0, maxCh1=0, avgCh1=0, medCh1=0;
    double minCh2=0, maxCh2=0, avgCh2=0, medCh2=0;
    double minDelta=0, maxDelta=0, avgDelta=0, medDelta=0;
    // Score stats per channel
    double minScoreCh1=0, maxScoreCh1=0, avgScoreCh1=0, medScoreCh1=0;
    double minScoreCh2=0, maxScoreCh2=0, avgScoreCh2=0, medScoreCh2=0;
    int count=0;
};

class LatencyMonitorDevice : public QObject {
    Q_OBJECT
public:
    explicit LatencyMonitorDevice(AWAHSipLib *lib);
    ~LatencyMonitorDevice();

    // Create and attach to conference bridge as a normal device
    // Returns true on success
    bool create(int direction /*0: both, 1: tx-only, 2: rx-only*/);
    void destroy();

    // Reset sliding window stats
    void resetStats();

    // Retrieve summary and raw measurements
    LatencyStatsSummary getSummaryStats();
    QVector<LatencyMeasurement> getMeasurements();

    // Conference slot and port
    int confSlot() const { return m_confSlot; }
    pjmedia_port *port() const { return m_port; }

private:
    AWAHSipLib *m_lib = nullptr;
    pjmedia_port *m_port = nullptr;
    LatencyMonitorState *m_state = nullptr;
    int m_confSlot = PJSUA_INVALID_ID;

    // Logging control per requirements
    bool m_loggedFirstSuccess = false;
    bool m_loggedFirstFailure = false; // after at least one success
    int  m_sinceLastSummary = 0;

    void maybeLogOnNewMeasurement(const LatencyMeasurement &m);
    LatencyStatsSummary computeSummary(const QVector<LatencyMeasurement> &ms) const;
};

#endif // LATENCY_MONITOR_DEVICE_H


