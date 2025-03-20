#ifndef PJENDPOINT_H
#define PJENDPOINT_H

#include <QObject>
#include <pjsua2.hpp>
using namespace pj;

class AWAHSipLib;

class PJEndpoint : public Endpoint
{
public :
    ~PJEndpoint();
    static PJEndpoint *instance();

    void onNatDetectionComplete(const OnNatDetectionCompleteParam &prm);
    void onNatCheckStunServersComplete(const OnNatCheckStunServersCompleteParam &prm);
    void onTransportState(const OnTransportStateParam &prm);
    void onTimer(const OnTimerParam &prm);
    void onSelectAccount(OnSelectAccountParam &prm);
    void onIpChangeProgress(OnIpChangeProgressParam &prm);

    void setAwahLibrary(AWAHSipLib *lib);

private:
    PJEndpoint();

    static PJEndpoint *pjEndpointInstance;
    AWAHSipLib* m_lib;
};

#endif // PJENDPOINT_H
