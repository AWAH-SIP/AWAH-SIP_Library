#include "gpiorouter.h"
#include "gpiodevice.h"

GpioRouter *GpioRouter::GpioRouterInstance = NULL;
GpioRouter *GpioRouter::instance(QObject *parent)
{
    if(GpioRouterInstance == NULL)
        GpioRouterInstance = new GpioRouter(parent);

    return GpioRouterInstance;
}

GpioRouter::GpioRouter(QObject *parent)
    : QObject(parent)
{
    delete GpioRouterInstance;
    GpioRouterInstance = NULL;
}

void GpioRouter::connectGpioPort(QString srcSlotId, QString destSlotId, bool inverted, bool persistant)
{
    for (auto & route : m_routes) {
        if(route.srcSlotId == srcSlotId && route.destSlotId == destSlotId){
            return;
        }
    }
    s_gpioRoute newRoute;
    newRoute.srcSlotId = srcSlotId;
    newRoute.destSlotId = destSlotId;
    newRoute.inverted = inverted;
    newRoute.persistant = persistant;
    m_routes.append(newRoute);
    emit gpioRoutesChanged(m_routes);
    processGpioStates();
}

void GpioRouter::disconnectGpioPort(QString srcSlotId, QString destSlotId)
{
    bool changed = false;
    QMutableListIterator<s_gpioRoute> i(m_routes);
    while(i.hasNext()){
        s_gpioRoute& route = i.next();
        if(route.srcSlotId == srcSlotId && route.destSlotId == destSlotId){
            i.remove();
            changed = true;
        }
    }
    if(changed) {
        emit gpioRoutesChanged(m_routes);
        processGpioStates();
    }
}

void GpioRouter::changeGpioCrosspoint(QString srcSlotId, QString destSlotId, bool inverted)
{
    for (auto & route : m_routes) {
        if (route.srcSlotId == srcSlotId && route.destSlotId == destSlotId){
            route.inverted = inverted;
        }
    }
    emit gpioRoutesChanged(m_routes);
    processGpioStates();
}

void GpioRouter::registerDevice(s_IODevices& deviceInfo, GpioDevice* device, QStringList gpiNames, QStringList gpoNames)
{
    for (uint i = 0; i < deviceInfo.inChannelCount; i++) {
        s_gpioPort newPort;
        if ((int)i < gpiNames.size())
            newPort.name = QString("%1: %2").arg(deviceInfo.inputname, gpiNames.at(i));
        else
            newPort.name = QString("%1 Ch:%2").arg(deviceInfo.inputname, QString::number(i+1));
        newPort.slotId = QString("GI:%1-Ch:%2").arg(deviceInfo.uid, QString::number(i+1));
        newPort.device = device;
        newPort.channel = i;
        newPort.lastState = false;
        m_portList.srcPorts.append(newPort);
    }
    for (uint i = 0; i < deviceInfo.outChannelCount; i++) {
        s_gpioPort newPort;
        if ((int)i < gpoNames.size())
            newPort.name = QString("%1: %2").arg(deviceInfo.outputame, gpoNames.at(i));
        else
            newPort.name = QString("%1 Ch:%2").arg(deviceInfo.outputame, QString::number(i+1));
        newPort.slotId = QString("GO:%1-Ch:%2").arg(deviceInfo.uid, QString::number(i+1));
        newPort.device = device;
        newPort.channel = i;
        newPort.lastState = false;
        m_portList.destPorts.append(newPort);
    }
    emit gpioRoutesTableChanged(m_portList);
    processGpioStates();
    // TODO: save HERE!
}

void GpioRouter::removeDevice(s_IODevices& deviceInfo)
{
    bool save = false;
    QMutableListIterator<s_gpioPort> i(m_portList.srcPorts), o(m_portList.destPorts);
    while(i.hasNext()){
        s_gpioPort& port = i.next();
        if (port.slotId.contains(deviceInfo.uid)) {
            i.remove();
            save = true;
        }
    }
    while(o.hasNext()){
        s_gpioPort& port = o.next();
        if (port.slotId.contains(deviceInfo.uid)) {
            o.remove();
            save = true;
        }
    }
    removeAllRoutesfromDevice(deviceInfo.uid);
    if (save) {
        emit gpioRoutesTableChanged(m_portList);
        processGpioStates();
        // TODO: save HERE!
    }
}

const QMap<QString, bool> GpioRouter::getGpioStates()
{
    QMap<QString, bool> ret;
    for(auto & srcPort : m_portList.srcPorts) {
        ret[srcPort.slotId] = srcPort.lastState;
    }
    for(auto & destPort : m_portList.destPorts) {
        ret[destPort.slotId] = destPort.lastState;
    }
    return ret;
}

void GpioRouter::processGpioStates()
{
    QMap<QString, bool> gpiStates, calculatedStates, changedStates;
    for(auto & srcPort : m_portList.srcPorts) {
        bool state = srcPort.device->getGPI(srcPort.channel);
        if(srcPort.lastState != state) {
            changedStates[srcPort.slotId] = state;
            srcPort.lastState = state;
        }
        gpiStates[srcPort.slotId] = state;
    }
    for (auto & route : m_routes) {
        bool routeState = gpiStates[route.srcSlotId];
        routeState ^= route.inverted;
        calculatedStates[route.destSlotId] |= routeState;
    }
    for(auto & destPort : m_portList.destPorts) {
        bool state = calculatedStates.value(destPort.slotId, false);
        if(destPort.lastState != state) {
            destPort.lastState = state;
            destPort.device->setGPO(destPort.channel, state);
            changedStates[destPort.slotId] = state;
        }
    }
    emit gpioStateChanged(changedStates);
}

s_gpioPort* GpioRouter::getPortFromId(QString slotId)
{
    if(slotId.startsWith("GI:")) {
        for(auto & srcPort : m_portList.srcPorts) {
            if(srcPort.slotId == slotId) {
                return &srcPort;
            }
        }
    } else {
        for(auto & destPort : m_portList.destPorts) {
            if(destPort.slotId == slotId) {
                return &destPort;
            }
        }
    }
    return nullptr;
}

void GpioRouter::removeAllRoutesfromDevice(QString uid)
{
    bool changed = false;
    QMutableListIterator<s_gpioRoute> i(m_routes);
    while(i.hasNext()){
        s_gpioRoute& route = i.next();
        if(route.srcSlotId.contains(uid) || route.destSlotId.contains(uid)){
            i.remove();
            changed = true;
        }
    }
    if(changed) {
        emit gpioRoutesChanged(m_routes);
        processGpioStates();
    }
}
