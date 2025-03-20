#ifndef LIBGPIOD_DEVICE_H
#define LIBGPIOD_DEVICE_H

#include "gpiodevice.h"
#include <QObject>
#include <QThread>
#include <QElapsedTimer>

#ifdef AWAH_libgpiod
#include <gpiod.hpp>

#define DEBOUNCE_TIMEOUT 100

class libgpiod_InputRunner;
#endif

class libgpiod_Device : public GpioDevice
{
    Q_OBJECT
public:
    explicit libgpiod_Device(s_IODevices& deviceInfo);
    ~libgpiod_Device();
    void setGPI(uint number, bool state) override;
    void setGPO(uint number, bool state) override;
    bool getGPI(uint number) const override { return m_inValues.at(number) == 1; };
    bool getGPO(uint number) const override { return m_outValues.at(number) == 1; };
    static QJsonObject getGpioCaps();

private slots:
    void getLineEvent(uint number, bool state);
    void logMessage(uint level, QString msg);

private:
    void setOutputs();

    QString m_chipName;
    QVector<uint> m_inOffsets, m_outOffsets;
    QVector<int> m_inValues, m_outValues;
    uint m_inCount, m_outCount;
    bool m_valid = true;
    bool m_hasGPI = false, m_hasGPO = false;

#ifdef AWAH_libgpiod
   gpiod::chip m_chip;
   gpiod::line_bulk m_outLines;
   libgpiod_InputRunner* m_inputRunner;
#endif

};

#ifdef AWAH_libgpiod
class libgpiod_InputRunner : public QThread
{
    Q_OBJECT
public:
    explicit libgpiod_InputRunner(QString chipName, QVector<uint> offsets, QObject *parent = nullptr);
    void stop();

signals:
    void inputEvent(uint number, bool state);
    void logMessage(uint level, QString msg);

private:
    void run() override;
    void checkEvent(const gpiod::line_event& event);

    QString m_chipName;
    QVector<uint> m_offsets;
    QVector<bool> m_state;
    QVector<QElapsedTimer> m_timers;
    bool m_valid = true;
    gpiod::chip m_chip;
    gpiod::line_bulk m_lines;

};

#endif

#endif // LIBGPIOD_DEVICE_H
