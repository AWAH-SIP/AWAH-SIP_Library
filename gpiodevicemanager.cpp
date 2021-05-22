#include "gpiodevicemanager.h"


GpioDeviceManager *GpioDeviceManager::GpioDeviceManagerInstance = NULL;
GpioDeviceManager *GpioDeviceManager::instance(QObject *parent)
{
    if(GpioDeviceManagerInstance == NULL) {
        GpioDeviceManagerInstance = new GpioDeviceManager(parent);
        GpioDeviceManagerInstance->createStaticOnDev();
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
        if(devInfo.devicetype != AccountGpioDevice)
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
    VirtualGpioDev* dev = create(1, 0, QString("Static ON"));
    dev->setGPI(0, true);
    emit dev->gpioChanged();
}
