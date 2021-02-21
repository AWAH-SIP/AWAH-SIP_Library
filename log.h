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

#ifndef LOG_H
#define LOG_H

#include <QObject>
#include <QDebug>
#include <QDate>
#include <QTime>
#include "pjlogwriter.h"

#define LOGSIZE (1024 * 1024) * 2 //log size in bytes (1024 * 1024 = 1MB)  2MB
#define LOGFILES 50


class AWAHSipLib;

class Log : public QObject
{
    Q_OBJECT
public:
    explicit Log(QObject *parent, AWAHSipLib *parentLib);
    void writePJSUALog(const QString& msg);
    void writeLog(unsigned int loglevel, const QString& msg);
    QStringList readNewestLog();
    QString logFolderName = "logs";

signals:
    void logMessage(QString msg);

private:
    AWAHSipLib *m_lib;
    PJLogWriter *m_pjLogWriter;

    bool initLogging();
    void initLogFileName();

    /**
    * @brief deletes old log files, only the last ones are kept
    */
    void deleteOldLogs();

    QString logFileName;
};

#endif // LOG_H
