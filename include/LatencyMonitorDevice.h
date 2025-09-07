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

/**
 * @brief Aggregate summary of latency statistics over the sliding window
 */
struct LatencyStatsSummary {
    // Per channel and delta
    double minCh1=0, maxCh1=0, avgCh1=0, medCh1=0;
    double minCh2=0, maxCh2=0, avgCh2=0, medCh2=0;
    double minDelta=0, maxDelta=0, avgDelta=0, medDelta=0;
    // Score stats per channel
    double minScoreCh1=0, maxScoreCh1=0, avgScoreCh1=0, medScoreCh1=0;
    double minScoreCh2=0, maxScoreCh2=0, avgScoreCh2=0, medScoreCh2=0;
    int count=0;
    // Success/miss counters
    int successCh1=0;
    int successCh2=0;
    int bothSuccess=0;
    int onlyCh1=0;
    int onlyCh2=0;
    int missCh1=0;
    int missCh2=0;
};

/**
 * @brief High-level device wrapper for the LatencyMonitor pjmedia port.
 * @details Manages lifecycle, conference attachment, measurement retrieval, and logging.
 */
class LatencyMonitorDevice : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Construct a LatencyMonitorDevice.
     * @param lib Owning library pointer used for logging and routing.
     */
    explicit LatencyMonitorDevice(AWAHSipLib *lib);
    /**
     * @brief Destructor.
     */
    ~LatencyMonitorDevice();

    /**
     * @brief Create the internal pjmedia port and prepare for conference attachment.
     * @param direction Direction: 0 both, 1 tx-only, 2 rx-only.
     * @param nameLabel Label used for bridge slot naming.
     * @return true on success.
     */
    bool create(int direction /*0: both, 1: tx-only, 2: rx-only*/, const QString &nameLabel = QString());

    /**
     * @brief Destroy the device and release resources.
     */
    void destroy();

    /**
     * @brief Clear the sliding window statistics and logging state.
     */
    void resetStats();

    /**
     * @brief Retrieve a computed summary over the current sliding window.
     * @return Summary statistics for latency and scores.
     */
    LatencyStatsSummary getSummaryStats();

    /**
     * @brief Retrieve raw per-burst measurements from the sliding window.
     * @return Vector of recent measurements.
     */
    QVector<LatencyMeasurement> getMeasurements();

    /**
     * @brief Conference bridge slot identifier.
     * @return Slot id, or PJSUA_INVALID_ID if not attached.
     */
    int confSlot() const { return m_confSlot; }
    /**
     * @brief Access the underlying pjmedia_port instance.
     * @return Non-owning pointer, or nullptr if not created.
     */
    pjmedia_port *port() const { return m_port; }

private:
    AWAHSipLib *m_lib = nullptr;
    pjmedia_port *m_port = nullptr;
    LatencyMonitorState *m_state = nullptr;
    int m_confSlot = PJSUA_INVALID_ID;

    // Logging control per requirements
    bool m_loggedFirstSuccessCh1 = false;
    bool m_loggedFirstSuccessCh2 = false;
    bool m_loggedFirstFailureCh1 = false; // after at least one success on ch1
    bool m_loggedFirstFailureCh2 = false; // after at least one success on ch2
    bool m_successSeenCh1 = false;
    bool m_successSeenCh2 = false;
    int  m_sinceLastSummary = 0;

    /**
     * @brief Apply logging policy based on the newest measurement.
     * @param m Latest measurement received from the monitor.
     */
    void maybeLogOnNewMeasurement(const LatencyMeasurement &m);

    /**
     * @brief Compute stats summary from a snapshot of measurements.
     * @param ms Measurements to aggregate.
     * @return Aggregated statistics.
     */
    LatencyStatsSummary computeSummary(const QVector<LatencyMeasurement> &ms) const;
};

#endif // LATENCY_MONITOR_DEVICE_H


