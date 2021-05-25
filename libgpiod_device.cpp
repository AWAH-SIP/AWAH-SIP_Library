#include "libgpiod_device.h"

#include "awahsiplib.h"
#include "gpiorouter.h"

libgpiod_Device::libgpiod_Device(s_IODevices& deviceInfo)
    : GpioDevice(deviceInfo), m_chipName(deviceInfo.typeSpecificSettings["chipName"].toString()),
      m_inCount(deviceInfo.inChannelCount), m_outCount(deviceInfo.outChannelCount)
{

    QJsonArray inOffsetsArr = deviceInfo.typeSpecificSettings["inOffsets"].toArray();
    QJsonArray outOffsetsArr = deviceInfo.typeSpecificSettings["outOffsets"].toArray();
    QStringList inName, outName;

    for (uint i = 0; i < m_inCount; i++) {
        m_inValues.append(0);
        m_inOffsets.append((uint) inOffsetsArr.at(i).toInt());
        inName.append(QString("line %1").arg(inOffsetsArr.at(i).toInt()));
    }
    for (uint i = 0; i < m_outCount; i++) {
        m_outValues.append(0);
        m_outOffsets.append((uint) outOffsetsArr.at(i).toInt());
        outName.append(QString("line %1").arg(outOffsetsArr.at(i).toInt()));
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

    GpioRouter::instance()->registerDevice(m_deviceInfo, this, inName, outName);
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

QJsonObject libgpiod_Device::getGpioCaps()
{
    QJsonObject ret;
#ifdef AWAH_libgpiod
    QJsonObject parameter, item, gpiochipEnum, lineEnum;
    int i = 0;
    QStringList chips;
    try {
        for (auto& it: ::gpiod::make_chip_iter()) {
            qDebug() << it.name().c_str() << " ["
                      << it.label().c_str() << "] ("
                      << it.num_lines() << " lines)";
            QString chipName = QString::fromStdString(it.name());
            gpiochipEnum[chipName] = i;
            chips.append(chipName);
        }
        for (const QString &chip: chips ) {
            for (auto& lit: ::gpiod::line_iter(gpiod::chip(chip.toStdString()))) {
                if (lit.consumer().empty()) {
                    QString name = QString("%1 | %2 | %3").arg(chip, QString::number(lit.offset()), lit.name().empty() ? "unnamed" : QString::fromStdString(lit.name()));
                    qDebug() << name;
                    lineEnum[name] = (int) lit.offset();
                }
            }
        }
    }  catch (const std::exception &e) {
        qDebug() << "libgpiod_Device::getGpioCaps(): Error: " << e.what();
    }
    item = QJsonObject();
    item["type"] = STRING;
    item["value"] = "";
    parameter["Name"] = item;
    item = QJsonObject();
    item["type"] = ENUM;
    item["value"] = 0;
    item["min"] = 0;
    item["max"] = 1024;
    item["enumlist"] = gpiochipEnum;
    parameter["GPIO Chip"] = item;
    lineEnum["No Line"] = -1;
    item = QJsonObject();
    item["type"] = ENUM;
    item["value"] = -1;
    item["min"] = 0;
    item["max"] = 1024;
    item["enumlist"] = lineEnum;
    parameter["GPI 1"] = item;
    parameter["GPI 2"] = item;
    parameter["GPI 3"] = item;
    parameter["GPI 4"] = item;
    parameter["GPI 5"] = item;
    parameter["GPI 6"] = item;
    parameter["GPI 7"] = item;
    parameter["GPI 8"] = item;
    parameter["GPO 1"] = item;
    parameter["GPO 2"] = item;
    parameter["GPO 3"] = item;
    parameter["GPO 4"] = item;
    parameter["GPO 5"] = item;
    parameter["GPO 6"] = item;
    parameter["GPO 7"] = item;
    parameter["GPO 8"] = item;
    ret["devType"] = LinuxGpioDevice;
    ret["parameter"] = parameter;
#endif
    return ret;
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
    for (int i = 0; i < m_offsets.count(); i++) {
        QElapsedTimer timer;
        m_state.append(false);
        m_timers.append(timer);
        m_timers[i].start();
    }
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
        if(m_timers[number].msecsSinceReference() > DEBOUNCE_TIMEOUT && m_state[number] != state) {
            m_timers[number].start();
            m_state[number] = state;
            emit inputEvent((uint) number, state);
            emit logMessage(4, QString("libgpiod_InputRunner::checkEvent(): Input %1 is set to %2").arg(QString::number(number), QString(state ? "true" : "false")));
        }
    } else {
        emit logMessage(4, QString("libgpiod_InputRunner::checkEvent(): Got Event from Unknown Offset %1 with State %2").arg(QString::number(event.source.offset()), QString(state ? "true" : "false")));
    }
}

#endif
