#include "gpiodevicemanager.h"


GpioDeviceManager *GpioDeviceManager::GpioDeviceManagerInstance = NULL;
GpioDeviceManager *GpioDeviceManager::instance(QObject *parent)
{
    if(GpioDeviceManagerInstance == NULL) {
        GpioDeviceManagerInstance = new GpioDeviceManager(parent);
        GpioDeviceManagerInstance->createStaticOnDev();
        uint inarr[2] = {16, 17}, outarr[2] = {22, 26};
        GpioDeviceManagerInstance->create(2,2,"Test-libgpiod", "gpiochip0", inarr, outarr);
    }

    return GpioDeviceManagerInstance;
}

GpioDeviceManager::GpioDeviceManager(QObject *parent)
    : QObject(parent)
{
    delete GpioDeviceManagerInstance;
    GpioDeviceManagerInstance = NULL;
    m_router = GpioRouter::instance(this);
}

GpioDeviceManager::~GpioDeviceManager()
{
    QMapIterator<QString, GpioDevice*> i(m_devPtr);
    while (i.hasNext()) {
        i.next();
        removeDevice(i.key());
    }
    delete m_router;
}

VirtualGpioDev* GpioDeviceManager::create(uint inCount, uint outCount, QString devName)
{
    s_IODevices newDev;
    newDev.devicetype = VirtualGpioDevice;
    newDev.uid = createNewUID();
    newDev.inputname = newDev.outputame = devName;
    newDev.RecDevID = newDev.PBDevID = 0;
    if(inCount > MAX_GPIO)  newDev.inChannelCount = MAX_GPIO;
    else                    newDev.inChannelCount = inCount;
    if(outCount > MAX_GPIO) newDev.outChannelCount = MAX_GPIO;
    else                    newDev.outChannelCount = outCount;
    VirtualGpioDev* ret = new VirtualGpioDev(newDev);
    appendDevice(ret);
    return ret;
}

LogicGpioDev* GpioDeviceManager::create(DeviceType type, uint outCount, QString devName)
{
    LogicGpioDev* ret;
    s_IODevices newDev;
    switch (type) {
    case LogicAndGpioDevice:
    case LogicOrGpioDevice:
        newDev.devicetype = type;
        newDev.uid = createNewUID();
        newDev.inputname = newDev.outputame = devName;
        newDev.RecDevID = newDev.PBDevID = 0;
        newDev.inChannelCount = 1;
        if (outCount > MAX_LOGIC_OUT) newDev.outChannelCount = MAX_LOGIC_OUT;
        else if (outCount < 2)      newDev.outChannelCount = 2;
        else                        newDev.outChannelCount = outCount;
        ret = new LogicGpioDev(newDev);
        break;
    default:
        return nullptr;
    }
    appendDevice(ret);
    return ret;
}

AccountGpioDev* GpioDeviceManager::create(const s_account &account)
{
    s_IODevices newDev;
    newDev.devicetype = AccountGpioDevice;
    newDev.uid = account.uid;
    newDev.inputname = newDev.outputame = account.name;
    newDev.RecDevID = newDev.PBDevID = 0;
    newDev.inChannelCount = 2;
    newDev.outChannelCount = 0;
    AccountGpioDev* ret = new AccountGpioDev(newDev);
    appendDevice(ret);
    return ret;
}

libgpiod_Device* GpioDeviceManager::create(uint inCount, uint outCount, QString devName, QString chipName, uint *inOffsets, uint *outOffsets)
{
    s_IODevices newDev;
    newDev.devicetype = LinuxGpioDevice;
    newDev.uid = createNewUID();
    newDev.inputname = newDev.outputame = devName;
    newDev.RecDevID = newDev.PBDevID = 0;
    if(inCount > MAX_GPIO)  newDev.inChannelCount = MAX_GPIO;
    else                    newDev.inChannelCount = inCount;
    if(outCount > MAX_GPIO) newDev.outChannelCount = MAX_GPIO;
    else                    newDev.outChannelCount = outCount;

    QJsonArray inOffsetArr, outOffsetArr;
    for (uint i = 0; i < newDev.inChannelCount; i++) {
        inOffsetArr.append(QJsonValue((int) inOffsets[i]));
    }
    for (uint i = 0; i < newDev.outChannelCount; i++) {
        outOffsetArr.append(QJsonValue((int) outOffsets[i]));
    }

    newDev.typeSpecificSettings["chipName"] = chipName;
    newDev.typeSpecificSettings["inOffsets"] = inOffsetArr;
    newDev.typeSpecificSettings["outOffsets"] = outOffsetArr;

    libgpiod_Device* ret = new libgpiod_Device(newDev);
    appendDevice(ret);
    return ret;
}

GpioDevice* GpioDeviceManager::createGeneric(s_IODevices &deviceInfo)
{
    GpioDevice* ret;
    if(deviceInfo.uid.isEmpty())
        deviceInfo.uid = createNewUID();
    deviceInfo.RecDevID = deviceInfo.PBDevID = 0;
    switch (deviceInfo.devicetype) {
    case VirtualGpioDevice:
        if(deviceInfo.inputname.isEmpty())
            deviceInfo.inputname = "Virtual GPI";
        if(deviceInfo.outputame.isEmpty())
            deviceInfo.outputame = "Virtual GPO";
        if(deviceInfo.inChannelCount > MAX_GPIO)
            deviceInfo.inChannelCount = MAX_GPIO;
        if(deviceInfo.outChannelCount > MAX_GPIO)
            deviceInfo.outChannelCount = MAX_GPIO;
        ret = new VirtualGpioDev(deviceInfo);
        break;
    case LogicAndGpioDevice:
    case LogicOrGpioDevice:
        deviceInfo.inChannelCount = 1;
        if(deviceInfo.inputname.isEmpty())
            deviceInfo.inputname = (deviceInfo.devicetype == LogicAndGpioDevice) ? "Logic &": "Logic ≥1";
        if(deviceInfo.outputame.isEmpty())
            deviceInfo.outputame = (deviceInfo.devicetype == LogicAndGpioDevice) ? "Logic &": "Logic ≥1";
        if (deviceInfo.outChannelCount > MAX_LOGIC_OUT)
            deviceInfo.outChannelCount = MAX_LOGIC_OUT;
        else if (deviceInfo.outChannelCount < 2)
            deviceInfo.outChannelCount = 2;
        ret = new LogicGpioDev(deviceInfo);
        break;
    case LinuxGpioDevice:
        if(deviceInfo.inputname.isEmpty())
            deviceInfo.inputname = "Linux GPI Device";
        if(deviceInfo.outputame.isEmpty())
            deviceInfo.outputame = "Linux GPO Device";
        if(deviceInfo.inChannelCount > MAX_GPIO)
            deviceInfo.inChannelCount = MAX_GPIO;
        if(deviceInfo.outChannelCount > MAX_GPIO)
            deviceInfo.outChannelCount = MAX_GPIO;
        if(deviceInfo.typeSpecificSettings.isEmpty())
            return nullptr;
        ret = new libgpiod_Device(deviceInfo);
        break;
    default:
        return nullptr;

    }
    appendDevice(ret);
    return ret;
}

void GpioDeviceManager::removeDevice(QString uid)
{
    if (m_devPtr.contains(uid)) {
        disconnect(m_devPtr[uid], &GpioDevice::gpioChanged, m_router, &GpioRouter::processGpioStates);
        delete m_devPtr[uid];
        m_devPtr.remove(uid);
        emit gpioDevicesChanged(getGpioDevices());
    }
}

GpioDevice* GpioDeviceManager::getDeviceByUid(QString uid)
{
    return m_devPtr.value(uid, nullptr);
}

QList<s_IODevices> &GpioDeviceManager::getGpioDevices()
{
    m_devList.clear();
    QMapIterator<QString, GpioDevice*> i(m_devPtr);
    while (i.hasNext()) {
        i.next();
        GpioDevice* dev = i.value();
        s_IODevices devInfo = dev->getDeviceInfo();
        if(devInfo.devicetype != AccountGpioDevice && devInfo.devicetype != m_staticOnUid)
            m_devList.append(devInfo);
    }
    return m_devList;
}

void GpioDeviceManager::setLib(AWAHSipLib *lib)
{
    m_lib = lib;
}

void GpioDeviceManager::appendDevice(GpioDevice *device)
{
    if(device) {
        m_devPtr[device->getDeviceInfo().uid] = device;
        connect(device, &GpioDevice::gpioChanged, m_router, &GpioRouter::processGpioStates, Qt::QueuedConnection);
        emit gpioDevicesChanged(getGpioDevices());
    }
}

void GpioDeviceManager::createStaticOnDev()
{
    if(m_staticOnUid.isEmpty()){
        VirtualGpioDev* dev = create(1, 0, QString("Static ON"));
        m_staticOnUid = dev->getDeviceInfo().uid;
        dev->setGPI(0, true);
        emit dev->gpioChanged();
    }
}
