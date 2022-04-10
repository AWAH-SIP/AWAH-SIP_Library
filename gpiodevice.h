#ifndef GPIODEVICE_H
#define GPIODEVICE_H

#include <QObject>
#include "types.h"

#define MAX_GPIO        16
#define MAX_LOGIC_OUT    8

class GpioRouter;
class GpioDeviceManager;

class GpioDevice : public QObject
{
    Q_OBJECT
public:
    explicit GpioDevice(s_IODevices& deviceInfo);
    ~GpioDevice();

    virtual bool getGPI(uint number) const = 0;
    virtual bool getGPO(uint number) const = 0;
    virtual void setGPI(uint number, bool state) = 0;
    virtual void setGPO(uint number, bool state) = 0;
    const  s_IODevices& getDeviceInfo() const { return m_deviceInfo;}

signals:
    void gpioChanged();

protected:

    s_IODevices m_deviceInfo;

    friend class GpioRouter;
};

class VirtualGpioDev : public GpioDevice
{
    Q_OBJECT
public:
    explicit VirtualGpioDev(s_IODevices& deviceInfo);
    ~VirtualGpioDev() {};
    void setGPI(uint number, bool state) override;
    void setGPO(uint number, bool state) override;
    bool getGPI(uint number) const override { return m_inState[number]; };
    bool getGPO(uint number) const override { return m_outState[number]; };

signals:

private:
    uint m_inCount, m_outCount;
    bool m_inState[MAX_GPIO], m_outState[MAX_GPIO];

};

class LogicGpioDev : public GpioDevice
{
    Q_OBJECT
public:
    explicit LogicGpioDev(s_IODevices& deviceInfo);
    ~LogicGpioDev() {};
    void setGPI(uint number, bool state) override;
    void setGPO(uint number, bool state) override;
    bool getGPI(uint number) const override { Q_UNUSED(number); return m_inState; };
    bool getGPO(uint number) const override { return m_outState[number]; };

signals:

private:
    uint m_outCount;
    bool m_inState, m_outState[MAX_LOGIC_OUT];

};

class AccountGpioDev : public GpioDevice
{
    Q_OBJECT
public:
    explicit AccountGpioDev(s_IODevices& deviceInfo);
    ~AccountGpioDev() {};
    void setGPI(uint number, bool state) override;
    void setGPO(uint number, bool state) override;
    void setRegistered(bool state);
    void setConnected(bool state);
    bool getGPI(uint number) const override { return m_inState[number]; };
    bool getGPO(uint number) const override { return m_outState[number]; };

signals:

private:
    bool m_inState[10], m_outState[8];

};

class AudioCrosspointDev : public GpioDevice
{
    Q_OBJECT
public:
    explicit AudioCrosspointDev(s_IODevices& deviceInfo);
    ~AudioCrosspointDev();
    void setGPI(uint number, bool state) override;
    void setGPO(uint number, bool state) override;
    bool getGPI(uint number) const override { Q_UNUSED(number); return m_inState; };
    bool getGPO(uint number) const override { Q_UNUSED(number); return m_outState; };
    static QJsonObject getGpioCaps();

private slots:
    void audioRoutesChanged(QList<s_audioRoutes> audioRoutes);
    void audioRoutesTableChanged(const s_audioPortList& portList);

signals:

private:
    int getSlots();
    void setCrosspoint();

    s_audioRoutes m_route;
    bool m_inState, m_outState;

};

#endif // GPIODEVICE_H
