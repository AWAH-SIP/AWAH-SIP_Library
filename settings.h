/*
 * Copyright (C) 2016 - 2022 Andy Weiss, Adi Hilber
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <QObject>
#include "types.h"

class AWAHSipLib;

class Settings : public QObject
{
    Q_OBJECT
public:
    explicit Settings(AWAHSipLib *parentLib, QObject *parent = nullptr);

    /**
    * @brief Load IO device config (audio interfaces, tone generator, GPIO etc) from settings file
    * and add them to the conference bridge
    */
    void loadIODevConfig();

    /**
    * @brief save current IO device config (audio interfaces, tone generator etc) to the settings file
    */
    void saveIODevConfig();

    /**
    * @brief load current GPIO device config from the settings file
    */
    void loadGpioDevConfig();

    /**
    * @brief load GPIO routes from settings file
    */
    void loadGpioRoutes();

    /**
    * @brief load configured buddies from settings file
    */
    void loadBuddies();

    /**
    * @brief save buddies to settings file
    */
    void saveBuddies();

    /**
    * @brief Load endpoint and media settings from settings file and restart endpoint to activate them
    */
    void loadSettings();

    /**
    * @brief load current account config from settings file
    */
    void loadAccConfig();

    /**
    * @brief save current account config to the settings file
    */
    void saveAccConfig();

    /**
    * @brief load audio routes from settings file
    */
    int loadAudioRoutes();

    /**
    * @brief save audio routes to the settings file
    */
    int saveAudioRoutes();

    /**
    * @brief load custom source names from settings file
    */
    void loadCustomSourceNames();

    /**
    * @brief save custiom source names to the settings file
    */
    void saveCustomSourceNames();

    /**
    * @brief load custom destination names from settings file
    */
    void loadCustomDestinationNames();

    /**
    * @brief save custiom destination names to the settings file
    */
    void saveCustomDestinationNames();

    /**
    * @brief get the log file path
    * @return QString logfilepath
    */
    QString getLogPath();

    /**
    * @brief get all editable general settings
    * @return a QJsonObject with an Object for each setting category. Each setting in a category is a new
    * object which incluedes the value,  max and min and for certain parameters a enum object with default values
    */
    const QJsonObject* getSettings();

    /**
    * @brief set general settings
    * @param a QJsonObect with all edited parameters
    */
    void setSettings(QJsonObject editedSettings);

    /**
    * @brief get all codec priorities
    * @return JSON Object with the priority for every codec.
    * @details for each Codec, an object is generated, with codecname as key
    *          the object contains the priority, an enumlist, a min and a max field
    */
    const QJsonObject getCodecPriorities();

    /**
    * @brief set general settings
    * @param a JSON Object with codecname as key and priority as value
    */
    void setCodecPriorities(QJsonObject CodecPriorities);

public slots:
    /**
    * @brief save current GPIO device config to the settings file
    */
    void saveGpioDevConfig();

    /**
    * @brief save GPIO routes to the settings file
    */
    void saveGpioRoutes();

//    /**
//     * @brief save Buddies to the settings file
//     */
//    void saveBuddies();

signals:


private:

    AWAHSipLib* m_lib;

    /**
    * @brief all settings are stored in this object
    * @details the object has several objects in it: AudioSettins GeneralSettings
    * each objects holds different settings
    * each setting consists of a value, type, min, max, enumlist
    */
    QJsonObject m_settings;

    bool m_AccountsLoaded = false;
    bool m_IoDevicesLoaded = false;
    bool m_AudioRoutesLoaded = false;
    bool m_GpioDevicesLoaded = false;
    bool m_GpioRoutesLoaded = false;
    bool m_BuddiesLoaded = false;

private slots:
    void loadIODevConfigLater();

};

#endif // SETTINGS_H
