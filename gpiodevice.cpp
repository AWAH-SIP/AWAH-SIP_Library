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
    for(int i=0; i<10;i++){
        m_inState[i] = false;
    }
    for(int i=0; i<8;i++){
        m_outState[i] = false;
    }
    GpioRouter::instance()->registerDevice(m_deviceInfo, this, {"Registered", "Connected", "DTMF 0/1", "DTMF 2/3", "DTMF 4/5", "DTMF 6/7", "DTMF 8/9", "DTMF */#", "DTMF A/B", "DTMF C/D"},{"DTMF 0/1", "DTMF 2/3", "DTMF 4/5", "DTMF 6/7", "DTMF 8/9", "DTMF */#", "DTMF A/B", "DTMF C/D"});
}

void AccountGpioDev::setGPI(uint number, bool state)
{    
    if( number < 10) {
        m_inState[number] = state;
        emit gpioChanged();
    }
}

void AccountGpioDev::setGPO(uint number, bool state)
{
    if(number < 8 && m_outState[number] != state) {
        m_outState[number] = state;
        sendDTMF(number);
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
        if(!state){
            for(int i=2;i<10;i++){
                setGPI(i,false);
            }
        }
        emit gpioChanged();
    }
    if(state){
        for(int i=0;i<8;i++){
            sendDTMF(i);
        }
    }
}

void AccountGpioDev::setFromDTMF(char DTMF)
{
    switch (DTMF) {
    case '0':
        setGPI(2,0);
        break;
    case '1':
        setGPI(2,1);
        break;
    case '2':
        setGPI(3,0);
        break;
    case '3':
        setGPI(3,1);
        break;
    case '4':
        setGPI(4,0);
        break;
    case '5':
        setGPI(4,1);
        break;
    case '6':
        setGPI(5,0);
        break;
    case '7':
        setGPI(5,1);
        break;
    case '8':
        setGPI(6,0);
        break;
    case '9':
        setGPI(6,1);
        break;
    case 'A':
        setGPI(8,0);
        break;
    case 'B':
        setGPI(8,1);
        break;
    case 'C':
        setGPI(9,0);
        break;
    case 'D':
        setGPI(9,1);
        break;
    case '*':
        setGPI(7,0);
        break;
    case '#':
        setGPI(7,1);
        break;
    default:
        break;
    }
}

void AccountGpioDev::sendDTMF(uint number)
{
    char DTMFdigit = -1;
    switch (number) {
    case 0:
        DTMFdigit = m_outState[number] ? '1' : '0';
        break;
    case 1:
        DTMFdigit = m_outState[number] ? '3' : '2';
        break;
    case 2:
        DTMFdigit = m_outState[number] ? '5' : '4';
        break;
    case 3:
        DTMFdigit = m_outState[number] ? '7' : '6';
        break;
    case 4:
        DTMFdigit = m_outState[number] ? '9' : '8';
        break;
    case 5:
        DTMFdigit = m_outState[number] ? '#' : '*';
        break;
    case 6:
        DTMFdigit = m_outState[number] ? 'B' : 'A';
        break;
    case 7:
        DTMFdigit = m_outState[number] ? 'D' : 'C';
        break;
    default:
        break;
    }
    AWAHSipLib::instance()->sendDTMFtoAllCalls(m_deviceInfo.uid,DTMFdigit);
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
    item["type"] = ENUM_STRING;
    item["value"] = "";
    item["min"] = 0;
    item["max"] = 1024;
    item["enumlist"] = srcPortEnum;
    parameter["Source Port"] = item;
    item = QJsonObject();
    item["type"] = ENUM_STRING;
    item["value"] = "";
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
