#ifndef GPIODEVICEMANAGER_H
#define GPIODEVICEMANAGER_H

#include <QObject>
#include "gpiodevice.h"
#include "gpiorouter.h"
#include "libgpiod_device.h"

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
    AudioCrosspointDev* create(const s_audioRoutes& route, QString devName);
    libgpiod_Device* create(uint inCount, uint outCount, QString devName, QString chipName, uint inOffsets[MAX_GPIO], uint outOffsets[MAX_GPIO]);
    GpioDevice* createGeneric(s_IODevices& deviceInfo);
    QString createGpioDev(QJsonObject& newDev);
    void removeDevice(QString uid);
    GpioDevice* getDeviceByUid(QString uid);
    QList<s_IODevices>& getGpioDevices();
    const QJsonObject getGpioDevTypes() const;

    void setLib(AWAHSipLib *lib);

signals:
    void gpioDevicesChanged(QList<s_IODevices>& deviceList);

private:
    explicit GpioDeviceManager(QObject *parent = nullptr);
    void appendDevice(GpioDevice* device);
    void createStaticOnDev();

    static GpioDeviceManager* GpioDeviceManagerInstance;
    AWAHSipLib* m_lib;
    GpioRouter* m_router;
    QMap<QString, GpioDevice*> m_devPtr;
    QList<s_IODevices> m_devList;
    QString m_staticOnUid;

};

#endif // GPIODEVICEMANAGER_H
