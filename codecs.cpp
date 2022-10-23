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

#include "codecs.h"
#include <QSettings>
#include "awahsiplib.h"

#define THIS_FILE		"codecs.cpp"

Codecs::Codecs(AWAHSipLib *parentLib, QObject *parent) : QObject(parent), m_lib(parentLib)
{

}

QList<s_codec> Codecs::listCodecs(){
    QSettings settings("awah", "AWAHsipConfig");
    int priority;
    m_codecs.clear();
    foreach(const CodecInfo codec, m_lib->m_pjEp->codecEnum2())
    {
        s_codec newCodec;
        QJsonObject item, enumitems;
        QStringList CodecInfo = QString::fromStdString(codec.codecId).split("/");       // the list contains now name , bitrate , channelcount ("speex", "16000", "1")
        priority = settings.value("settings/CodecPriority/"+QString::fromStdString(codec.codecId),"127").toInt();
        newCodec.codecParameters = getCodecParam(QString::fromStdString(codec.codecId));
        newCodec.encodingName = CodecInfo.at(0);
        newCodec.priority = priority;
        m_lib->m_pjEp->codecSetPriority(codec.codecId, priority);
        if(CodecInfo.at(0) == "L16"){
            newCodec.displayName = "Linear";
            item = QJsonObject();
            item["type"] = ENUM_INT;
            item["value"] = CodecInfo.at(1).toInt();
            item["min"] = 8000;
            item["max"] = 48000;
            enumitems = QJsonObject();
            enumitems["8k"] = 8000;
            enumitems["16k"] = 16000;
            enumitems["32k"] = 32000;
            enumitems["44,1k"] = 44100;
            enumitems["48k"] = 48000;
            item["enumlist"] = enumitems;
            newCodec.codecParameters["Clockrate"] = item;
            item = QJsonObject();
            item["type"] = ENUM_INT;
            item["value"] = CodecInfo.at(2).toInt();
            item["max"] = 2;
            enumitems = QJsonObject();
            enumitems["mono"] = 1;
            enumitems["stereo"] = 2;
            item["enumlist"] = enumitems;
            newCodec.codecParameters["Channelcount"] = item;
        }
        else if(CodecInfo.at(0) == "opus"){
            newCodec.displayName = "Opus";
            newCodec.encodingName = "opus/48000/2";

        }
        else if(CodecInfo.at(0) == "speex"){
            item = QJsonObject();
            item["type"] = ENUM_INT;
            item["value"] = CodecInfo.at(1).toInt();
            item["min"] = 8000;
            item["max"] = 32000;
            enumitems = QJsonObject();
            enumitems["8k"] = 8000;
            enumitems["16k"] = 16000;
            enumitems["32k"] = 32000;
            item["enumlist"] = enumitems;
            newCodec.codecParameters["Clockrate"] = item;
            item = QJsonObject();
            item["type"] = ENUM_INT;
            item["value"] = CodecInfo.at(2).toInt();
            item["max"] = 1;
            enumitems = QJsonObject();
            enumitems["mono"] = 1;
            item["enumlist"] = enumitems;
            newCodec.codecParameters["Channelcount"] = item;
        }
        else if(CodecInfo.at(0) == "PCMU"){
            newCodec.displayName = "G711 u-Law";
            newCodec.encodingName = "PCMU/8000/1";
        }
        else if(CodecInfo.at(0) == "PCMA"){
            newCodec.displayName = "G711 A-Law";
            newCodec.encodingName = "PCMA/8000/1";
        }
        else if(CodecInfo.at(0) == "G722"){
            newCodec.displayName = "G722";
            newCodec.encodingName = "G722/16000/1";
        }
        else if(CodecInfo.at(0) == "iLBC"){
            newCodec.displayName = "iLBC";
            newCodec.encodingName = "iLBC/8000/1";
        }
        else if(CodecInfo.at(0) == "AMR"){
            newCodec.displayName = "AMR";
            newCodec.encodingName = "AMR/8000/1";
        }
        else if(CodecInfo.at(0) == "GSM"){
            newCodec.displayName = "GSM";
            newCodec.encodingName = "GSM/8000/1";
        }
        else{
            newCodec.displayName = CodecInfo.at(0);
        }
        m_codecs.append(newCodec);
    }
    settings.sync();
    return m_codecs;
}

QList<s_codec> Codecs::getActiveCodecs()
{
    QList<s_codec> codecList;
    bool codecExists = false;
    for(auto &Codec : m_codecs)
    {
        if(Codec.priority){
            codecExists = false;
            for(auto &list : codecList){
                if(Codec.displayName == list.displayName){
                    codecExists = true;
                }
            }
            if(!codecExists){
                codecList.append(Codec);
            }
        }
    }

//    firstMatch.displayName = "First matching Codec";
//    codecList.append(firstMatch);
    return codecList;
}

void Codecs::selectCodec(s_codec &codec){
    QString codecId;

    qDebug() << "codec encodingname: " << codec.displayName << " existing encodig name ";
    if(!codec.encodingName.contains("/")){
        qDebug() << "Generate codec encodingname: " << codec.displayName << " existing encodig name " << codec.encodingName;
        codec.encodingName = codec.encodingName + "/" + QString::number(codec.codecParameters["Clockrate"].toObject()["value"].toInt()) + "/" + QString::number(codec.codecParameters["Channelcount"].toObject()["value"].toInt());
        qDebug() << "new encodingname: "  << codec.encodingName;
    }
    bool codecFound = false;
    foreach(const CodecInfo codecinfo, m_lib->m_pjEp->codecEnum2())
    {
        if(QString::fromStdString(codecinfo.codecId) == codec.encodingName){
            m_lib->m_pjEp->codecSetPriority(codecinfo.codecId, 255);
            codecFound = true;
            }
        else{
            m_lib->m_pjEp->codecSetPriority(codecinfo.codecId, 0);
            }
    }
    if(!codecFound){
        m_lib->m_Log->writeLog(3,(QString("select Codec: could not select codec: codec ") + codecId + ": codec not found"));
    }
}

const QJsonObject Codecs::getCodecParam(QString codecId)
{
    CodecParam param;
    try{
    param = m_lib->m_pjEp->codecGetParam(codecId.toStdString());
    }  catch (Error &err) {
        AWAHSipLib::instance()->m_Log->writeLog(1, (QString("getCodecParam: failed: ") + err.info().c_str()));
    }
    return getCodecParam(param, codecId);
}


const QJsonObject Codecs::getCodecParam(CodecParam PJcodecParam, QString codecId)
{    
    QJsonObject codecparam, item, enumitems;

    item = QJsonObject();
    item["type"] = ENUM_INT;
    item["value"] = PJcodecParam.setting.cng;
    item["min"] = 0;
    item["max"] = 1;
    enumitems = QJsonObject();
    enumitems["off"] = 0;
    enumitems["on"] = 1;
    item["enumlist"] = enumitems;
    codecparam["Confort Noise generator"] = item;

    item = QJsonObject();
    item["type"] = INTEGER;
    item["value"] = (int)PJcodecParam.setting.frmPerPkt;
    item["min"] = 1;
    item["max"] = 10;
    enumitems = QJsonObject();
    codecparam["Frames per Packet"] = item;

    foreach(const pj::CodecFmtp fmtp, PJcodecParam.setting.decFmtp){
        item = QJsonObject();
        bool paramParsed = false;
        if(strcmp(fmtp.name.c_str(),"useinbandfec")==0){            // show only known parameters
            item["type"] = INTEGER;
            item["value"] = atoi(fmtp.val.c_str());                    // opus fec
            item["min"] = 0;
            item["max"] = 100;                                        // to test!
            codecparam["Inband FEC"]= item;
            paramParsed = true;
        }
        else if(strcmp(fmtp.name.c_str(),"maxaveragebitrate")==0){
            paramParsed = true;
        }
        else if(strcmp(fmtp.name.c_str(),"cbr")==0){
            paramParsed = true;
        }
        else if(strcmp(fmtp.name.c_str(),"stereo")==0){
            item["type"] = ENUM_INT;                                // opus test
            item["value"] = atoi(fmtp.val.c_str());
            item["min"] = 0;
            item["max"] = 1;
             enumitems = QJsonObject();
            enumitems["mono"] = 0;
            enumitems["stereo"] = 1;
           item["enumlist"] = enumitems;
            codecparam["stereo"]= item;
            paramParsed = true;
        }
        else if(strcmp(fmtp.name.c_str(),"sprop-stereo")==0){                           // don't populate menu just mark as parsed
            item["type"] = ENUM_INT;                                // opus test
            item["value"] = atoi(fmtp.val.c_str());
            item["min"] = 0;
            item["max"] = 1;
             enumitems = QJsonObject();
            enumitems["mono"] = 0;
            enumitems["stereo"] = 1;
           item["enumlist"] = enumitems;
            codecparam["sprop-stereo"]= item;
            paramParsed = true;
        }

        else if(strcmp(fmtp.name.c_str(),"mode")==0 && codecId.startsWith("iLBC")){
                item["type"] = ENUM_INT;                                //iLBC Mode
                item["value"] = atoi(fmtp.val.c_str());
                item["min"] = 20;
                item["max"] = 30;
                 enumitems = QJsonObject();
                enumitems["20ms"] = 20;
                enumitems["30ms"] = 30;
               item["enumlist"] = enumitems;
                codecparam["Mode"]= item;
                paramParsed = true;
        }

        if(!paramParsed){
            m_lib->m_Log->writeLog(4,QString("getCodecParam: dec fmtp unparsed key/value: ") +fmtp.name.c_str() + " value: " + fmtp.val.c_str());
        }
    }

    foreach(const pj::CodecFmtp fmtp, PJcodecParam.setting.encFmtp){
        m_lib->m_Log->writeLog(4,QString("getCodecParam: unparsed encoding key/value: ") +fmtp.name.c_str());
    }

    if(codecId.startsWith("opus",Qt::CaseInsensitive)){

        pjmedia_codec_opus_config opus_cfg;
        pjmedia_codec_opus_get_config(&opus_cfg);
        item = QJsonObject();
        item["type"] = ENUM_INT;
        item["value"] = (int) opus_cfg.bit_rate;
        item["min"] = 6000;
        item["max"] = 510000;
        enumitems = QJsonObject();
        enumitems[" 16kBit/s"] =16000;
        enumitems[" 32kBit/s"] =32000;
        enumitems[" 64kBit/s"] =64000;
        enumitems[" 96kBit/s"] =96000;
        enumitems["128kBit/s"] =128000;
        enumitems["192kBit/s"] =192000;
        enumitems["256kBit/s"] =256000;
        enumitems["320kBit/s"] =320000;
        enumitems["448kBit/s"] =448000;
        item["enumlist"] = enumitems;
        codecparam["Bit rate"] = item;

        item = QJsonObject();
        item["type"] = ENUM_INT;
        item["value"] = opus_cfg.cbr;
        item["min"] = 0;
        item["max"] = 1;
        enumitems = QJsonObject();
        enumitems ["variable"]  = 0;
        enumitems ["constant"] = 1;
        item["enumlist"] = enumitems;
        codecparam["Bit rate mode"] = item;

        item = QJsonObject();
        item["type"] = INTEGER;
        item["value"] = (int) opus_cfg.channel_cnt;
        item["min"] = 1;
        item["max"] = (int) m_lib->epCfg.medConfig.channelCount;
        codecparam["Channelcount"] = item;

        item = QJsonObject();
        item["type"] = ENUM_INT;
        item["value"] = (int)opus_cfg.complexity;
        item["min"] = 1;
        item["max"] = 10;
        enumitems = QJsonObject();
        enumitems [" 1"] = 1;
        enumitems [" 2"] = 2;
        enumitems [" 3"] = 3;
        enumitems [" 4"] = 4;
        enumitems [" 5"] = 5;
        enumitems [" 6"] = 6;
        enumitems [" 7"] = 7;
        enumitems [" 8"] = 8;
        enumitems [" 9"] = 9;
        enumitems ["10"] = 10;
        item["enumlist"] =enumitems;
        codecparam["Complexity"] = item;

        item = QJsonObject();
        item["type"] = ENUM_INT;
        item["value"] = (int)opus_cfg.frm_ptime;
        item["min"] = 2;                                         // todo schould be 2.5!!!!
        item["max"] = 60;
        enumitems = QJsonObject();
        enumitems [" 5ms"] = 5;
        enumitems ["10ms"] = 10;
        enumitems ["20ms"] = 20;
        enumitems ["40ms"] = 40;
        enumitems ["60ms"] = 60;
        item["enumlist"] =enumitems;
        codecparam["Frame ptime"] = item;

        item = QJsonObject();
        item["type"] = ENUM_INT;
        item["value"] = (int)opus_cfg.sample_rate;
        item["min"] = 8000;
        item["max"] = 48000;
        enumitems = QJsonObject();
        enumitems [" 8kHz (narrowband)"] = 8000;
        enumitems ["12kHz (medium-band)"] = 12000;
        enumitems ["16kHz (wideband)"] = 16000;
        enumitems ["24kHz (super-wideband)"] = 24000;
        enumitems ["48kHz (fullband)"]= 48000;
        item["enumlist"] = enumitems;
        codecparam["Clockrate"] = item;
    }
    return codecparam;
}


#define CodecsCount 20          // please remove me and make it nicer!
int Codecs::setCodecParam(s_codec codec)
{
    pj_status_t status = 1;
    pjmedia_codec_mgr* mgr;
    pjmedia_endpt *medep;
    unsigned int count = CodecsCount;
    pjmedia_codec_info  info[CodecsCount] , * selected;
    pjmedia_codec_param param;
    pjmedia_codec_opus_config opus_cfg;

    medep = pjsua_get_pjmedia_endpt();
    mgr= pjmedia_endpt_get_codec_mgr(medep);
    pjmedia_codec_mgr_enum_codecs(mgr,&count,info,nullptr);
    for (unsigned int i = 0; i< count;i++){
        QString pjCodecID = pj2Str(info[i].encoding_name) + "/" + QString::number(info[i].clock_rate) + "/" + QString::number(info[i].channel_cnt);
        if ( pjCodecID == codec.encodingName){
             pjmedia_codec_mgr_get_default_param(mgr,&info[i], &param);          // get the default parameters in case not all available parameters are included in the JSON
             selected = &info[i];
             status = PJ_SUCCESS;
        }
    }
    if (status) {
        m_lib->m_Log->writeLog(3,QString("setCodecPar: codec with the ID: ") + codec.encodingName +  " not found");
        return status;
    }

    QJsonObject::iterator i;
    for (i = codec.codecParameters.begin(); i != codec.codecParameters.end(); ++i) {
        qDebug() << "found codec param: " << i.key() << " value : " << i.value();
        if (i.key() == "Confort Noise generator"){
            param.setting.cng = i.value().toObject()["value"].toInt();
        }
        if (i.key() == "Frames per Packet"){
            param.setting.frm_per_pkt = i.value().toObject()["value"].toInt();
        }

        if (i.key() == "Inband FEC"){                                                                      // todo test me!
            for(int j = 0; j<param.setting.dec_fmtp.cnt; j++){
                 if(pj_strcmp2(&param.setting.dec_fmtp.param[j].name,"useinbandfec")==0){
                    param.setting.dec_fmtp.param[j].val = str2Pj(i.value().toObject()["value"].toString()) ;
                 }
            }
        }

        if (i.key() == "stereo"){                                                                      // todo test me!
                    for(int j = 0; j<param.setting.dec_fmtp.cnt; j++){
                         if(pj_strcmp2(&param.setting.dec_fmtp.param[j].name,"stereo")==0){
                            param.setting.dec_fmtp.param[j].val = str2Pj(i.value().toObject()["value"].toString()) ;
                         }
                    }
                }

        if (i.key() == "sprop-stereo"){                                                                      // todo test me!
                    for(int j = 0; j<param.setting.dec_fmtp.cnt; j++){
                         if(pj_strcmp2(&param.setting.dec_fmtp.param[j].name,"sprop-stereo")==0){
                            param.setting.dec_fmtp.param[j].val = str2Pj(i.value().toObject()["value"].toString()) ;
                         }
                    }
                }

        if (i.key() == "Mode" && codec.encodingName.startsWith("iLBC")){
            for(int j = 0; j<param.setting.dec_fmtp.cnt; j++){
                 if(pj_strcmp2(&param.setting.dec_fmtp.param[j].name,"mode")==0){               //iLBC Mode
                    param.setting.dec_fmtp.param[j].val = str2Pj(i.value().toObject()["value"].toString()) ;                   // todo does not work!!!
                 }
            }
        }

        if (i.key() == "Bitrate"){                                                                      // todo test me!
            for(int j = 0; j<param.setting.dec_fmtp.cnt; j++){
                 if(pj_strcmp2(&param.setting.dec_fmtp.param[j].name,"bitrate")==0){
                    param.setting.dec_fmtp.param[j].val = str2Pj(i.value().toObject()["value"].toString()) ;
                 }
            }
        }
    }

    status =  pjmedia_codec_mgr_set_default_param(mgr,selected,&param);
    if (status ) {
        char buf[50];
        pj_strerror	(status,buf,sizeof (buf) );
        m_lib->m_Log->writeLog(2,QString("setCodecPar: Error while setting codec default parameter") + buf);
        return status;
    }

    if(codec.encodingName.startsWith("opus",Qt::CaseInsensitive)){                                     // do the opus stuff
         pjmedia_codec_opus_get_config(&opus_cfg);
         QJsonObject::iterator it;
         for (it = codec.codecParameters.begin(); it != codec.codecParameters.end(); ++it) {
             if (it.key() == "Bit rate"){
                opus_cfg.bit_rate  = it.value().toObject()["value"].toInt();
             }
             if (it.key() == "Bit rate mode"){
                 opus_cfg.cbr  = it.value().toObject()["value"].toInt();
             }
             if (it.key() == "Channelcount"){
                opus_cfg.channel_cnt  = it.value().toObject()["value"].toInt();
             }
             if (it.key() == "Complexity"){
                opus_cfg.complexity  = it.value().toObject()["value"].toInt();
             }
             if (it.key() == "Frame ptime"){
               opus_cfg.frm_ptime  = it.value().toObject()["value"].toInt();
             }
             if (it.key() == "Clockrate"){
               opus_cfg.sample_rate = it.value().toObject()["value"].toInt();
             }
          }

        status = pjmedia_codec_opus_set_default_param(&opus_cfg, &param);
        if (status ) {
            char buf[50];
            pj_strerror	(status,buf,sizeof (buf) );
            m_lib->m_Log->writeLog(2,QString("setCodecPar: Error while setting opus parameter ") + buf);
            return status;
        }
     }
    return PJ_SUCCESS;
}

