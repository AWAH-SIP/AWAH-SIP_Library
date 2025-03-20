#ifndef GPIOROUTER_H
#define GPIOROUTER_H

#include <QObject>
#include "types.h"

class GpioDevice;

class GpioRouter : public QObject
{
    Q_OBJECT
public:
    ~GpioRouter() {};
    static GpioRouter* instance(QObject *parent = nullptr);

    const QList<s_gpioRoute>& getGpioRoutes() { return m_routes; };
    const s_gpioPortList& getGpioPortsList() { return m_portList; };
    void connectGpioPort(QString srcSlotId, QString destSlotId, bool inverted, bool persistant = true);
    void disconnectGpioPort(QString srcSlotId, QString destSlotId);
    void changeGpioCrosspoint(QString srcSlotId, QString destSlotId, bool inverted);
    void registerDevice(s_IODevices& deviceInfo, GpioDevice* device, QStringList gpiNames = QStringList(), QStringList gpoNames = QStringList());
    void removeDevice(s_IODevices& deviceInfo);
    const QMap<QString, bool> getGpioStates();

signals:
    void gpioRoutesChanged(const QList<s_gpioRoute>& routes);
    void gpioRoutesTableChanged(const s_gpioPortList& portList);
    void gpioStateChanged(const QMap<QString, bool> changedGpios);

public slots:
    void processGpioStates();

protected:

private:
    explicit GpioRouter(QObject *parent = nullptr);
    s_gpioPort *getPortFromId(QString slotId);
    void removeAllRoutesfromDevice(QString uid);

    static GpioRouter *GpioRouterInstance;
    s_gpioPortList m_portList;
    QList<s_gpioRoute> m_routes;

};

#endif // GPIOROUTER_H
