/*
 * LatencyMonitor pjmedia port (8kHz, stereo, 20ms) - generates periodic PN/MLS bursts
 * and estimates RTT using normalized cross-correlation on received audio.
 */

#ifndef LATENCYMON_PORT_H
#define LATENCYMON_PORT_H

#include <QtGlobal>
#include <QString>
#include <QVector>

extern "C" {
#include <pjmedia.h>
#include <pjlib.h>
}

/**
 * @brief Opaque forward declaration for internal state
 */
struct LatencyMonitorState;

/**
 * @brief Per-measurement record returned by the latency monitor
 *
 * - rttCh1Ms/rttCh2Ms: Per-channel RTT in milliseconds (negative if not detected)
 * - deltaMs: Absolute difference between channel RTTs (negative if any channel invalid)
 * - corrCh1/corrCh2: Per-channel NCC scores in [0,1]
 * - tsMs: Wallclock timestamp in milliseconds
 * - successCh1/successCh2: True when that channel met the min correlation threshold
 */
struct LatencyMeasurement {
    double rttCh1Ms = -1.0;
    double rttCh2Ms = -1.0;
    double deltaMs  = -1.0;
    double corrCh1  = 0.0;
    double corrCh2  = 0.0;
    quint64 tsMs    = 0;   // wallclock in ms
    bool successCh1 = false; // true if channel 1 met min correlation
    bool successCh2 = false; // true if channel 2 met min correlation
};

/**
 * @brief Optional notification callback for new measurement decisions.
 * @details Called once per burst after detection attempt completes. For failures,
 *          rttCh1Ms and rttCh2Ms will be negative.
 */
typedef void (*latencymon_notify_cb)(void *user, const LatencyMeasurement &m);

/**
 * @brief Create latency monitor port (stereo, 8kHz, 20ms frames)
 * @param pool PJSIP memory pool for allocations
 * @param nameLabel Descriptive label for the port (used in bridge)
 * @param appCtx Optional application context pointer passed through to callbacks
 * @param p_port [out] Created pjmedia_port
 * @param p_state [out] Created LatencyMonitorState
 * @return PJ_SUCCESS on success
 */
pj_status_t latencymon_port_create(pj_pool_t *pool,
                                   const QString &nameLabel,
                                   void *appCtx, // optional AWAHSipLib* or logger context
                                   pjmedia_port **p_port,
                                   LatencyMonitorState **p_state);

/**
 * @brief Destroy port and free internal resources
 * @param st LatencyMonitor state to destroy
 */
void latencymon_port_destroy(LatencyMonitorState *st);

/**
 * @brief Reset sliding statistics window (clears stored measurements)
 * @param st LatencyMonitor state
 */
void latencymon_reset_stats(LatencyMonitorState *st);

/**
 * @brief Retrieve a copy of the recent measurements (5-minute window)
 * @param st LatencyMonitor state
 * @return Vector of recent LatencyMeasurement
 */
QVector<LatencyMeasurement> latencymon_get_measurements(LatencyMonitorState *st);

/**
 * @brief Set how often summary logging occurs (every N measurements)
 * @param st LatencyMonitor state
 * @param everyNMeasurements Positive integer
 */
void latencymon_set_log_interval(LatencyMonitorState *st, int everyNMeasurements);

/**
 * @brief Set repeat interval between bursts in milliseconds
 * @param st LatencyMonitor state
 * @param intervalMs Minimum 500 ms
 */
void latencymon_set_repeat_interval_ms(LatencyMonitorState *st, int intervalMs);

/**
 * @brief Set minimum NCC correlation required per channel to accept an RTT
 * @param st LatencyMonitor state
 * @param minCorr In range [0..1]
 */
void latencymon_set_min_corr(LatencyMonitorState *st, double minCorr);

/**
 * @brief Register/unregister notification for one-shot per-burst result
 * @param st LatencyMonitor state
 * @param user Opaque pointer passed back to callback
 * @param cb Callback to receive measurements (nullptr to unregister)
 */
void latencymon_set_notify(LatencyMonitorState *st, void *user, latencymon_notify_cb cb);

#endif // LATENCYMON_PORT_H


