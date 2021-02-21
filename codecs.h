/*
 * Copyright (C) 2016 - 2021 Andy Weiss, Adi Hilber
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

#ifndef CODECS_H
#define CODECS_H

#include <QObject>
#include "types.h"

class AWAHSipLib;

class Codecs : public QObject
{
    Q_OBJECT
public:
    explicit Codecs(AWAHSipLib *parentLib, QObject *parent = nullptr);

    /**
    * @brief list all available codecs
    * @return a stringlist with the names of all supported codecs
    */
    QStringList listCodec();

    /**
    * @brief select a codec to force AWAHsip to a specific codec
    * @brief note that all other codecs will be disabled
    */
    void selectCodec(QString selectedcodec);

    /**
    * @brief get all editable parameters for a codec
    * @param codecId the ID of the codec  e.g. "opus/48000/2"
    * @return a QJsonObject with an Object for every parameter.
    * in the parameter Object included is value, max and min and for certain parameters a enum Object with default values
    */
    const QJsonObject getCodecParam(QString codecId);

    /**
    * @brief set parameters for a certain codec
    * @param codecId the ID of the codec  e.g. "opus/48000/2"
    * @param a QJsonObject with an Object for every changed parameter.
    * @return PJ_Success on sucess or -1 if one or more parameters could not have been set
    */
    int setCodecParam(QString codecId, QJsonObject CodecParam);

signals:


private:
    AWAHSipLib* m_lib;

};

#endif // CODECS_H
