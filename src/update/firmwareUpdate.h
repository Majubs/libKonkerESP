/*
    Firmware update class. Part of Konker FW.
    @author Maria Júlia Berriel de Sousa
*/
#ifndef _FW_UPDATE_H_
#define _FW_UPDATE_H_

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include "../globals.h"
//#include "../konker.h"
#include "../protocols/protocol.h"

enum State {RUNNING, UPDATING, REBOOT, UPDATED};

class ESPHTTPKonkerUpdate: public ESP8266HTTPUpdate
{
    public:
        ESPHTTPKonkerUpdate(HTTPClient client, String currentVersion);
        ESPHTTPKonkerUpdate();
        ~ESPHTTPKonkerUpdate();

        void setClient(Protocol client);
        Protocol getClient();
        void setVersion(String version);
        String getVersion();

        t_httpUpdate_return update(String newVersion);
        void updateSucessCallBack(char newVersion[]);
        bool checkForUpdate();
        bool runUpdate(UPDATE_SUCCESS_CALLBACK_SIGNATURE);
        void performUpdate();
        //To be called during setup
        bool checkFirstBoot();
        void sendUpdateConfirmation(String newVersion);
    private:
        //KonkerDevice _device;
        HTTPClient _client;
        const String _fwEndpoint;
        String _currentVersion;
        String _newVersion;
        State _deviceState;
        unsigned long _last_time_update_check;

        String getVersionFromPayload(String strPayload);
        void updateVersion(String newVersion);
        bool querryPlatform(String recvVersion);
};

#endif /* _FW_UPDATE_H_ */