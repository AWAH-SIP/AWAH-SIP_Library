#include "libgpiod_device.h"

#include "awahsiplib.h"
#include "gpiorouter.h"

libgpiod_Device::libgpiod_Device(s_IODevices& deviceInfo)
    : GpioDevice(deviceInfo), m_chipName(deviceInfo.typeSpecificSettings["chipName"].toString()),
      m_inCount(deviceInfo.inChannelCount), m_outCount(deviceInfo.outChannelCount)
{

    QJsonArray inOffsetsArr = deviceInfo.typeSpecificSettings["inOffsets"].toArray();
    QJsonArray outOffsetsArr = deviceInfo.typeSpecificSettings["outOffsets"].toArray();

    for (uint i = 0; i < m_inCount; i++) {
        m_inValues.append(0);
        m_inOffsets.append((uint) inOffsetsArr.at(i).toInt());
    }
    for (uint i = 0; i < m_outCount; i++) {
        m_outValues.append(0);
        m_outOffsets.append((uint) outOffsetsArr.at(i).toInt());
    }

#ifdef AWAH_libgpiod
    try {
        m_chip.open(m_chipName.toStdString());
        m_outLines = m_chip.get_lines(std::vector<uint>(m_outOffsets.begin(), m_outOffsets.end()));
        m_outLines.request({
                               QString("AWAH-SIP_Library_Outs").toStdString(),
                               gpiod::line_request::DIRECTION_OUTPUT,
                               0
                           }, std::vector<int>(m_outValues.begin(), m_outValues.end()));
    }  catch (const std::exception &e) {
        m_valid = false;
        AWAHSipLib::instance()->m_Log->writeLog(1, QString("libgpiod_Device(): Failed to open gpioDevice %1: %2").arg(m_chipName, e.what()));
    }

    if(m_valid) {
        m_inputRunner = new libgpiod_InputRunner(m_chipName, m_inOffsets);
        connect(m_inputRunner, &libgpiod_InputRunner::inputEvent, this, &libgpiod_Device::getLineEvent);
        connect(m_inputRunner, &libgpiod_InputRunner::logMessage, this, &libgpiod_Device::logMessage);
        connect(m_inputRunner, &libgpiod_InputRunner::finished, m_inputRunner, &QObject::deleteLater);
        m_inputRunner->start();
    }
#endif

    GpioRouter::instance()->registerDevice(m_deviceInfo, this);
}

libgpiod_Device::~libgpiod_Device()
{
#ifdef AWAH_libgpiod
    if (m_inputRunner)
        m_inputRunner->stop();
#endif
}

void libgpiod_Device::setGPI(uint number, bool state)
{
    if(number < m_inCount) {
        m_inValues[number] = state;
    }
}

void libgpiod_Device::setGPO(uint number, bool state)
{
    if(number < m_outCount) {
        if (m_outValues.at(number) != state ? 1 : 0) {
            m_outValues[number] = state;
            setOutputs();
        }
    }
}

void libgpiod_Device::getLineEvent(uint number, bool state)
{
    setGPI(number, state);
    emit gpioChanged();
}

void libgpiod_Device::logMessage(uint level, QString msg)
{
    AWAHSipLib::instance()->m_Log->writeLog(level, msg);
}

void libgpiod_Device::setOutputs()
{
    if(!m_valid)
        return;
#ifdef AWAH_libgpiod
    try {
        m_outLines.set_values(std::vector<int>(m_outValues.begin(), m_outValues.end()));
    }  catch (const std::exception &e) {
        AWAHSipLib::instance()->m_Log->writeLog(1, QString("libgpiod_Device::setOutputs(): Failed to set Outputs on gpioDevice %1: %2").arg(m_chipName, e.what()));
    }
#endif

}

#ifdef AWAH_libgpiod
libgpiod_InputRunner::libgpiod_InputRunner(QString chipName, QVector<uint> offsets, QObject *parent)
    : QThread(parent), m_chipName(chipName), m_offsets(offsets)
{
    try {
        m_chip.open(m_chipName.toStdString());
        m_lines = m_chip.get_lines(std::vector<uint>(m_offsets.begin(), m_offsets.end()));
        m_lines.request({
                               QString("AWAH-SIP_Library_Ins").toStdString(),
                               gpiod::line_request::EVENT_BOTH_EDGES,
                               0
                           });
    }  catch (const std::exception &e) {
        m_valid = false;
        emit logMessage(1, QString("libgpiod_InputRunner(): Failed to open gpioDevice %1: %2").arg(m_chipName, e.what()));
    }
}

void libgpiod_InputRunner::stop()
{
    m_valid = false;
}

void libgpiod_InputRunner::run()
{
    while (m_valid) {
        auto events = m_lines.event_wait(::std::chrono::seconds(1));
        if (events) {
            for (auto& it: events)
                checkEvent(it.event_read());
        }
    }
}

void libgpiod_InputRunner::checkEvent(const gpiod::line_event& event)
{
    bool state;
    if (event.event_type == gpiod::line_event::RISING_EDGE)
        state = true;
    else if (event.event_type == gpiod::line_event::FALLING_EDGE)
        state = false;
    else
        return;

    int number = m_offsets.indexOf(event.source.offset());
    if (number >= 0) {
        emit inputEvent((uint) number, state);
        emit logMessage(4, QString("libgpiod_InputRunner::checkEvent(): Input %1 is set to %2").arg(QString::number(number), QString(state ? "true" : "false")));
    } else {
        emit logMessage(4, QString("libgpiod_InputRunner::checkEvent(): Got Event from Unknown Offset %1 with State %2").arg(QString::number(event.source.offset()), QString(state ? "true" : "false")));
    }
}

#endif
