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

#include "log.h"
#include "awahsiplib.h"
#include <QDir>
#include <qcoreapplication.h>


#define THIS_FILE		"log.cpp"

Log::Log(QObject *parent, AWAHSipLib *parentLib) : QObject(parent), m_lib(parentLib)
{
    m_pjLogWriter = new PJLogWriter(this);
    m_lib->epCfg.logConfig.writer = m_pjLogWriter;
    logFolderName = m_lib->m_Settings->getLogPath();
    initLogging();
}


bool Log::initLogging()
{
    // Create folder for logfiles if not exists
    if(!QDir(logFolderName).exists()){
        QDir().mkdir(logFolderName);
    }

    deleteOldLogs(); //delete old log files
    initLogFileName(); //create the logfile name

    QFile outFile(logFileName);
    if(outFile.open(QIODevice::WriteOnly | QIODevice::Append)){
        return true;
    }
    else{
        return false;
    }
}

void Log::initLogFileName()
{
    logFileName = QString(logFolderName + "/Log_%1__%2.txt")
                .arg(QDate::currentDate().toString("yyyy_MM_dd"))
                    .arg(QTime::currentTime().toString("hh_mm"));
}


void Log::deleteOldLogs()
{
    QDir dir;
    dir.setFilter(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    dir.setSorting(QDir::Time | QDir::Reversed);
    dir.setPath(logFolderName);

    QFileInfoList list = dir.entryInfoList();
    if (list.size() <= LOGFILES){
        return; //no files to delete
    }
    else{
      for (int i = 0; i < (list.size() - LOGFILES +1); i++){
        QString path = list.at(i).absoluteFilePath();
        QFile file(path);
        file.remove();
        }
    }
}

void Log::writePJSUALog(const QString& msg){
    //check file size and if needed create new log!
    emit logMessage(msg.simplified());
    QFile outFileCheck(logFileName);
    int size = outFileCheck.size();

    if (size > LOGSIZE) {   //check current log size
      deleteOldLogs();
      initLogFileName();
    }
    QFile outFile(logFileName);
    outFile.open(QIODevice::WriteOnly | QIODevice::Append);
    QTextStream ts(&outFile);
    ts << QDate::currentDate().toString("yyyy_MM_dd") << " " << msg;
#ifdef QT_DEBUG
    qDebug() << msg;
#endif
}

void Log::writeLog(unsigned int loglevel, const QString& msg){
    if (loglevel <= m_lib->epCfg.logConfig.consoleLevel){
        emit logMessage(msg);
        QFile outFileCheck(logFileName);
        int size = outFileCheck.size();

        if (size > LOGSIZE){ //check current log size
            deleteOldLogs();
            initLogFileName();
        }
        QFile outFile(logFileName);
        outFile.open(QIODevice::WriteOnly | QIODevice::Append);
        QTextStream ts(&outFile);
        ts << QDate::currentDate().toString("yyyy_MM_dd") << " " << QTime::currentTime().toString("hh:mm:ss") << "               AWAHsip: " << msg << Qt::endl;
    }
#ifdef QT_DEBUG
    qDebug() << "AWAHsip: " << msg;
#endif
}

QStringList Log::readNewestLog(){
    QFile inputFile(logFileName);
    QStringList list;
    inputFile.open(QIODevice::ReadOnly);
        if (!inputFile.isOpen()) {
            list.append("error reading logfile!");
            return list;
        }
    QTextStream stream(&inputFile);
    QString line = stream.readLine();
    while (!line.isNull()) {
        /* process information */
        line = stream.readLine() ;
        list.append(line);
    }
    QStringList ret;
    ret = list.mid(list.count()-501, -1);        // restrict the log to the newest 500 entrys for
    return ret;
}
