#ifndef GPIODEVICEMANAGER_H
#define GPIODEVICEMANAGER_H

#include <QObject>
#include "gpiodevice.h"
#include "gpiorouter.h"

class AWAHSipLib;

class GpioDeviceManager : public QObject
{
    Q_OBJECT
public:
    ~GpioDeviceManager();
    static GpioDeviceManager* instance(QObject *parent = nullptr);

    VirtualGpioDev* create(uint inCount, uint outCount, QString devName);
    LogicGpioDev* create(DeviceType type, uint outCount, QString devName);
    AccountGpioDev* create(const s_account& account);
    GpioDevice* createGeneric(s_IODevices& deviceInfo);
    void removeDevice(QString uid);
    GpioDevice* getDeviceByUid(QString uid);
    const QList<s_IODevices>& getGpioDevices();

    void setLib(AWAHSipLib *lib);

signals:
    void gpioDevicesChanged(const QList<s_IODevices>& deviceList);

private:
    explicit GpioDeviceManager(QObject *parent = nullptr);
    void appendDevice(GpioDevice* device);
    void createStaticOnDev();

    static GpioDeviceManager* GpioDeviceManagerInstance;
    AWAHSipLib* m_lib;
    GpioRouter* m_router;
    QMap<QString, GpioDevice*> m_devPtr;
    QList<s_IODevices> m_devList;

};

#endif // GPIODEVICEMANAGER_H
