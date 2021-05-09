#include "gpiodevice.h"
#include "gpiorouter.h"
#include "gpiodevicemanager.h"

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
