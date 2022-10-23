#include "pjendpoint.h"

#include <QDebug>
#include "types.h"
#include "awahsiplib.h"

PJEndpoint *PJEndpoint::pjEndpointInstance = NULL;
PJEndpoint *PJEndpoint::instance()
{
    if(pjEndpointInstance == NULL)
        pjEndpointInstance = new PJEndpoint;

    return pjEndpointInstance;
}

PJEndpoint::PJEndpoint()
    : Endpoint(), m_lib(NULL)
{
    delete pjEndpointInstance;
    pjEndpointInstance = NULL;
}

PJEndpoint::~PJEndpoint()
{
    libDestroy();
}

void PJEndpoint::onNatDetectionComplete(const OnNatDetectionCompleteParam &prm)
{
    Q_UNUSED(prm);
}

void PJEndpoint::onNatCheckStunServersComplete(const OnNatCheckStunServersCompleteParam &prm)
{
    Q_UNUSED(prm);
}

void PJEndpoint::onTransportState(const OnTransportStateParam &prm)
{
    Q_UNUSED(prm);
}

void PJEndpoint::onTimer(const OnTimerParam &prm)
{
    Q_UNUSED(prm);
}

void PJEndpoint::onSelectAccount(OnSelectAccountParam &prm)
{
    int defaultAccId = PJSUA_INVALID_ID;
    for (auto& account : *m_lib->getAccounts()) {
        if (account.accountPtr->isDefault()) {
            defaultAccId = account.AccID;
            break;
        }
    }
    if(prm.accountIndex == defaultAccId && defaultAccId != PJSUA_INVALID_ID){
        pjsip_uri *uri, *reqUri;
        pjsip_sip_uri *sip_uri, *sip_reqUri;
        pjsua_acc_id id = prm.accountIndex;
        pjsip_rx_data *pjRData = (pjsip_rx_data*) prm.rdata.pjRxData;
        uri = pjRData->msg_info.to->uri;
        reqUri = pjRData->msg_info.msg->line.req.uri;

        if (!PJSIP_URI_SCHEME_IS_SIP(uri) &&
                !PJSIP_URI_SCHEME_IS_SIPS(uri) &&
                !PJSIP_URI_SCHEME_IS_SIP(reqUri) &&
                !PJSIP_URI_SCHEME_IS_SIPS(reqUri)) {
            return;
        }
        sip_uri = (pjsip_sip_uri*)pjsip_uri_get_uri(uri);
        sip_reqUri = (pjsip_sip_uri*)pjsip_uri_get_uri(reqUri);

        uint maxScore = 0;
        for (auto& account : *m_lib->getAccounts()) {
            if (!account.accountPtr->isValid())
                continue;
            uint score = 0;

            // Match URI from TO-Field of Request
            if (QString::compare(account.user, pj2Str(sip_uri->user)) == 0) {
                score |= 2;
            }

            // Match Requestline URI
            if (QString::compare(account.user, pj2Str(sip_reqUri->user)) == 0) {
                score |= 1;
            }

            if (score > maxScore) {
                id = account.AccID;
                maxScore = score;
            }
        }
        if(id != prm.accountIndex) {
            m_lib->m_Log->writeLog(3, QString("onSelectAccount(): Account changed for incoming Request for User [%1 | %2] to Account-ID %3")
                                   .arg(pj2Str(sip_uri->user), pj2Str(sip_reqUri->user), QString::number(id)));
            prm.accountIndex = id;
        }
    }
}

void PJEndpoint::onIpChangeProgress(OnIpChangeProgressParam &prm)
{
    pjsua_ip_change_param param;
    pjsua_ip_change_param_default(&param);
    param.restart_listener = true;
    param.restart_lis_delay = true;
    pjsua_acc_set_registration(prm.accId,true);
    pjsua_handle_ip_change(&param);
}

void PJEndpoint::setAwahLibrary(AWAHSipLib *lib)
{
    m_lib = lib;
}
