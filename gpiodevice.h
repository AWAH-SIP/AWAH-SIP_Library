#ifndef GPIODEVICE_H
#define GPIODEVICE_H

#include <QObject>
#include "types.h"

#define MAX_GPIO        8
#define MAX_LOGIC_OUT    4

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
    const  s_IODevices& getDeviceInfo() const { return m_deviceInfo;}

signals:
    void gpioChanged();

protected:
    virtual void setGPI(uint number, bool state) = 0;
    virtual void setGPO(uint number, bool state) = 0;

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
    DeviceType m_type;
    bool m_inState, m_outState[MAX_LOGIC_OUT];

};

class AccountGpioDev : public GpioDevice
{
    Q_OBJECT
public:
    explicit AccountGpioDev(s_IODevices& deviceInfo);
    ~AccountGpioDev() {};
    void setGPI(uint number, bool state) override;
    void setGPO(uint number, bool state) override { Q_UNUSED(number); Q_UNUSED(state); };
    void setRegistered(bool state);
    void setConnected(bool state);
    bool getGPI(uint number) const override { return m_inState[number]; };
    bool getGPO(uint number) const override { Q_UNUSED(number); return false; };

signals:

private:
    bool m_inState[2];

};

#endif // GPIODEVICE_H