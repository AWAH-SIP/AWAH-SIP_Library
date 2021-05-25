#include "gpiodevice.h"
#include "gpiorouter.h"
#include "gpiodevicemanager.h"
#include "awahsiplib.h"

GpioDevice::GpioDevice(s_IODevices &deviceInfo)
    : QObject(GpioDeviceManager::instance()), m_deviceInfo(deviceInfo)
{

}

GpioDevice::~GpioDevice()
{
    GpioRouter::instance()->removeDevice(m_deviceInfo);
}

VirtualGpioDev::VirtualGpioDev(s_IODevices &deviceInfo)
    : GpioDevice(deviceInfo), m_inCount(deviceInfo.inChannelCount), m_outCount(deviceInfo.outChannelCount)
{
    for (uint i = 0; i < m_inCount; i++) {
        m_inState[i] = false;
    }
    for (uint i = 0; i < m_outCount; i++) {
        m_outState[i] = false;
    }
    GpioRouter::instance()->registerDevice(m_deviceInfo, this);
}

void VirtualGpioDev::setGPI(uint number, bool state)
{
    if(number < m_inCount) {
        m_inState[number] = state;
    }
}

void VirtualGpioDev::setGPO(uint number, bool state)
{
    if(number < m_outCount) {
        m_outState[number] = state;
    }
}

LogicGpioDev::LogicGpioDev(s_IODevices &deviceInfo)
    : GpioDevice(deviceInfo), m_outCount(deviceInfo.outChannelCount)
{
    for (uint i = 0; i < m_outCount; i++) {
        m_outState[i] = false;
    }
    m_inState = false;
    GpioRouter::instance()->registerDevice(m_deviceInfo, this);
}

void LogicGpioDev::setGPI(uint number, bool state)
{
    Q_UNUSED(number);
    m_inState = state;
}

void LogicGpioDev::setGPO(uint number, bool state)
{
    if(number < m_outCount) {
        m_outState[number] = state;
    }
    bool inState = m_outState[0];
    for (uint i = 0; i < m_outCount; i++) {
        if (m_deviceInfo.devicetype == LogicAndGpioDevice)
            inState = inState && m_outState[i];
        else
            inState = inState || m_outState[i];
    }
    if (m_inState != inState) {
        m_inState = inState;
        emit gpioChanged();
    }
}

AccountGpioDev::AccountGpioDev(s_IODevices &deviceInfo)
    : GpioDevice(deviceInfo)
{
    m_inState[0] = false;
    m_inState[1] = false;
    GpioRouter::instance()->registerDevice(m_deviceInfo, this, {"Registered", "Connected"});
}

void AccountGpioDev::setGPI(uint number, bool state)
{
    if(number < 2) {
        m_inState[number] = state;
    }
}

void AccountGpioDev::setRegistered(bool state)
{
    if(m_inState[0] != state) {
        m_inState[0] = state;
        emit gpioChanged();
    }
}

void AccountGpioDev::setConnected(bool state)
{
    if(m_inState[1] != state) {
        m_inState[1] = state;
        emit gpioChanged();
    }
}

AudioCrosspointDev::AudioCrosspointDev(s_IODevices &deviceInfo)
    : GpioDevice(deviceInfo)
{
    m_inState = false;
    m_outState = false;

    m_route.fromJSON(deviceInfo.typeSpecificSettings["route"].toObject());
    m_route.persistant = false;
    getSlots();

    GpioRouter::instance()->registerDevice(m_deviceInfo, this, {"State"}, {"Set"});

    connect(AWAHSipLib::instance(), &AWAHSipLib::audioRoutesChanged, this, &AudioCrosspointDev::audioRoutesChanged);
    connect(AWAHSipLib::instance(), &AWAHSipLib::audioRoutesTableChanged, this, &AudioCrosspointDev::audioRoutesTableChanged);
    audioRoutesChanged(AWAHSipLib::instance()->getAudioRoutes());
    setCrosspoint();
}

AudioCrosspointDev::~AudioCrosspointDev()
{
    disconnect(AWAHSipLib::instance(), &AWAHSipLib::audioRoutesChanged, this, &AudioCrosspointDev::audioRoutesChanged);
    disconnect(AWAHSipLib::instance(), &AWAHSipLib::audioRoutesTableChanged, this, &AudioCrosspointDev::audioRoutesTableChanged);
}

void AudioCrosspointDev::setGPI(uint number, bool state)
{
    if(number == 0 && m_inState != state) {
        m_inState = state;
        emit gpioChanged();
    }
}

void AudioCrosspointDev::setGPO(uint number, bool state)
{
    if(number == 0 && m_outState != state) {
        m_outState = state;
        setCrosspoint();
    }
}

QJsonObject AudioCrosspointDev::getGpioCaps()
{
    QJsonObject ret, parameter, item, srcPortEnum, destPortEnum;
    const s_audioPortList &pList = AWAHSipLib::instance()->getConfPortsList();
    for (const auto & srcPort : pList.srcPorts) {
        srcPortEnum[srcPort.name] = pj2Str(srcPort.pjName);
    }
    for (const auto & destPort : pList.destPorts) {
        destPortEnum[destPort.name] = pj2Str(destPort.pjName);
    }
    item["type"] = STRING;
    item["value"] = " ";
    parameter["Name"] = item;
    item = QJsonObject();
    item["type"] = ENUM;
    item["value"] = 0;
    item["min"] = 0;
    item["max"] = 1024;
    item["enumlist"] = srcPortEnum;
    parameter["Source Port"] = item;
    item = QJsonObject();
    item["type"] = ENUM;
    item["value"] = 0;
    item["min"] = 0;
    item["max"] = 1024;
    item["enumlist"] = destPortEnum;
    parameter["Destination Port"] = item;
    item = QJsonObject();
    item["type"] = INTEGER;
    item["value"] = 0;
    item["min"] = -144;
    item["max"] = +20;
    parameter["Level"] = item;
    ret["devType"] = AudioCrosspointDevice;
    ret["parameter"] = parameter;
    return ret;
}

void AudioCrosspointDev::audioRoutesChanged(QList<s_audioRoutes> audioRoutes)
{
    bool found = false;
    for (auto & route : audioRoutes) {
        if(route.destDevName == m_route.destDevName && route.srcDevName == m_route.srcDevName
                && route.destSlot == m_route.destSlot && route.srcSlot == m_route.srcSlot) {
            found = true;
            break;
        }
    }
    if(m_inState != found) {
        m_inState = found;
        emit gpioChanged();
    }
}

void AudioCrosspointDev::audioRoutesTableChanged(const s_audioPortList& portList)
{
    Q_UNUSED(portList);
    getSlots();
}

int AudioCrosspointDev::getSlots()
{
    const QMap<int, QString> srcAudioSlotMap = AWAHSipLib::instance()->m_AudioRouter->getSrcAudioSlotMap();
    const QMap<int, QString> destAudioSlotMap = AWAHSipLib::instance()->m_AudioRouter->getDestAudioSlotMap();
    m_route.srcSlot = srcAudioSlotMap.key(m_route.srcDevName, -1);
    m_route.destSlot = destAudioSlotMap.key(m_route.destDevName, -1);
    if (m_route.srcSlot == -1 || m_route.destSlot == -1) {
        AWAHSipLib::instance()->m_Log->writeLog(1, QString("AudioCrosspointDev::getSlots(): Can not get Slots for AudioCrosspointGioDev %1. Route will not be set!").arg(m_deviceInfo.inputname));
        return -1;
    }
    return 0;
}

void AudioCrosspointDev::setCrosspoint()
{
    if (m_inState == m_outState || m_route.srcSlot == -1 || m_route.destSlot == -1)
        return;
    if (m_inState) {
        AWAHSipLib::instance()->disconnectConfPort(m_route.srcSlot, m_route.destSlot);
    } else {
        AWAHSipLib::instance()->connectConfPort(m_route.srcSlot, m_route.destSlot, m_route.level, false);
    }
}
