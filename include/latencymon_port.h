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

// Opaque forward declaration
struct LatencyMonitorState;

// Per-measurement record
struct LatencyMeasurement {
    double rttCh1Ms = -1.0;
    double rttCh2Ms = -1.0;
    double deltaMs  = -1.0;
    double corrCh1  = 0.0;
    double corrCh2  = 0.0;
    quint64 tsMs    = 0;   // wallclock in ms
};

// Optional notification callback for new measurement decisions.
// Called once per burst after detection attempt completes.
// For failures, rttCh1Ms and rttCh2Ms will be negative.
typedef void (*latencymon_notify_cb)(void *user, const LatencyMeasurement &m);

// Create latency monitor port (stereo, 8kHz, 20ms frames)
// nameLabel will be set as port label in the bridge
pj_status_t latencymon_port_create(pj_pool_t *pool,
                                   const QString &nameLabel,
                                   void *appCtx, // optional AWAHSipLib* or logger context
                                   pjmedia_port **p_port,
                                   LatencyMonitorState **p_state);

// Destroy port and free internal resources
void latencymon_port_destroy(LatencyMonitorState *st);

// Reset sliding statistics window (clears stored measurements)
void latencymon_reset_stats(LatencyMonitorState *st);

// Retrieve a copy of the recent measurements (5-minute window)
QVector<LatencyMeasurement> latencymon_get_measurements(LatencyMonitorState *st);

// Configuration helpers (optional)
void latencymon_set_log_interval(LatencyMonitorState *st, int everyNMeasurements);
void latencymon_set_repeat_interval_ms(LatencyMonitorState *st, int intervalMs);

// Register/unregister notification for one-shot per-burst result
void latencymon_set_notify(LatencyMonitorState *st, void *user, latencymon_notify_cb cb);

#endif // LATENCYMON_PORT_H


