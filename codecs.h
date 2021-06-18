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
    * @brief returns a list with all supported codecs, including the disabled ones.
    * @brief a codec can be listed more than once, e.g. L16 with 1 channel and with two channels for 48k for 44.1k etc.
    * @return a QList with all the codecs
    */
    QList<s_codec> listCodecs();

    /**
    * @brief returns a list with the enabled codecs, each codectype is listed only once
    * @return a QList with all the codecs
    */
    QList<s_codec> getActiveCodecs();


    /**
    * @brief select a codec to force AWAHsip to a specific codec
    * @brief note that all other codecs will be disabled
    * @param codec the codec you like to select. (only encodingName, channelCount and clockRate are needed)
    */
    void selectCodec(s_codec codec);

    /**
    * @brief get all editable parameters for a codec
    * @param codecId the ID of the codec  e.g. "opus/48000/2"
    * @return a QJsonObject with an Object for every parameter.
    * in the parameter Object included is value, max and min and for certain parameters a enum Object with default values
    */
    const QJsonObject getCodecParam(QString codecId);
    const QJsonObject getCodecParam(CodecParam PJcodecParam, QString codecId);

    /**
    * @brief set parameters for a certain codec
    * @param codec the codec witch has to be updated
    * @return PJ_Success on sucess or -1 if one or more parameters could not have been set
    */
    int setCodecParam(s_codec codec);

signals:


private:
    AWAHSipLib* m_lib;
    QList<s_codec> m_codecs;

};

#endif // CODECS_H
