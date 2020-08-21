/*
  Firmware update class. Part of Konker FW.
  @author Maria Júlia Berriel de Sousa
*/

#include "firmwareUpdate.h"

/**
 * Constructors
 */
ESPHTTPKonkerUpdate::ESPHTTPKonkerUpdate(Protocol *client, String * manifestEndpoint) : _fwEndpoint("/firmware/")
{
  _httpProtocol = client;
  _manifestEndpoint = *manifestEndpoint;
  _deviceState = RUNNING;
  _last_time_update_check = 0;
  manifest = nullptr;
}

ESPHTTPKonkerUpdate::ESPHTTPKonkerUpdate() : _manifestEndpoint("/_update"), _fwEndpoint("/firmware/")
{
  _deviceState = RUNNING;
  _last_time_update_check = 0;
  manifest = nullptr;

  DynamicJsonDocument currentFwInfoJson(512);

  if(jsonHelper.loadCurrentFwInfo(&currentFwInfoJson))
  {
    strcpy(currentFwInfo.version, currentFwInfoJson["version"]);
    strcpy(currentFwInfo.deviceID, currentFwInfoJson["device"]);
    strcpy(currentFwInfo.seqNumber, currentFwInfoJson["sequence_number"]);
    currentFwInfo.loaded = INFO_LOADED;

    // Serial not initialized yet
    // Log.trace("[UPDT] Information read: %s / %s / %s / %X\n", currentFwInfo.version, currentFwInfo.deviceID, currentFwInfo.seqNumber, currentFwInfo.loaded);
  }
  else
  {
    memset(&currentFwInfo, 0, sizeof(fw_info_t));
  }
}

/**
 * Destructor
 */
ESPHTTPKonkerUpdate::~ESPHTTPKonkerUpdate()
{
  if (manifest != nullptr)
  {
    delete manifest;
  }
}

void ESPHTTPKonkerUpdate::setProtocol(Protocol *client)
{
  _httpProtocol = client;
  _fwEndpoint += _httpProtocol->getUser();
  _manifestEndpoint = "sub/" + _httpProtocol->getUser() + _manifestEndpoint;
}

// void ESPHTTPKonkerUpdate::setFWchannel(String id)
// {
//   _fwEndpoint += id;
//   _manifestEndpoint = "sub/" + id + _manifestEndpoint;
// }

/** TODO still need this?
 * Perform FW update
 * @param version char*
 * @return success t_httpUpdate_return
 */
t_httpUpdate_return ESPHTTPKonkerUpdate::update(String newVersion)
{
  HTTPClient * pHttp;
  void * pHttpVoid = nullptr;
  t_httpUpdate_return ret = t_httpUpdate_return::HTTP_UPDATE_NO_UPDATES;

  if (_httpProtocol->checkConnection())
  {
    _httpProtocol->setupClient(pHttpVoid, _fwEndpoint + String("/binary "));
    pHttp = static_cast<HTTPClient *>(pHttpVoid);
    if(!pHttp)
    {
      pHttp = new HTTPClient;
    }
    Log.trace("[UPDT] Fetching binary at: %s/binary\n", _fwEndpoint.c_str());
    // pHttp->setURL(String("registry-data/") + _fwEndpoint + String("/binary "));

    updateGlobals::httpProtocolGlobal = _httpProtocol;
    updateGlobals::manifestEndpointGlobal = &_manifestEndpoint;
    ret = this->handleUpdate(*pHttp, manifest->getCurrentVersion(), false);

    if(ret == HTTP_UPDATE_OK)
    {
      if(manifest->checkChecksum(Update.md5String()))
      {
        this->sendStatusMessage(MSG_CHECKSUM_OK);
      }
      else
      {
        this->sendExceptionMessage(EXPT_CHECKSUM_NOK);
      }
    }
    Log.trace("[UPDT] Return message = %s\n", this->getLastErrorString().c_str());
  }

  return ret;
}

/**
 * Send FW update confirmation to platform
 * @param none
 * @return none
 */
void ESPHTTPKonkerUpdate::sendUpdateConfirmation()
{
  char * newVersion = manifest->getNewVersion();

  if(!_httpProtocol->checkConnection())
  {
    Log.trace("[UPDT] Cannot send confirmation\n");
    return;
  }

  Log.trace("[UPDT] Update ok, sending confirmation\n");

  // TODO change to REBOOTING
  String smsg=String("{\"version\": \"" + String(newVersion) + "\",\"status\":\"UPDATED\"}");
  int retCode = _httpProtocol->send("_update", String(smsg));


  Log.trace("Confirmantion sent: %s; Body: %s; httpCode: %d\n", _fwEndpoint.c_str(), smsg.c_str(), retCode);

  if (retCode == 1)
  {
    Serial.println("[UPDT callback] Success\n\n");
  }
  else
  {
    Serial.println("[UPDT callback] Failed\n\n");
  }
}

/**
 * Callbacks that'll be called during ESP8266HTTPUpdate.update
 */
void atUpdateStart()
{
  Log.trace("[UPDT callback] Starting update!!! Progress:\n");
}

void atUpdateProgress(int progress, int total)
{
  Log.trace("%d..", progress * 100 / total);
  if(progress == total) Serial.println("");
}

void ESPHTTPKonkerUpdate::sendFwReceivedMessage()
{
  ESPHTTPKonkerUpdate updateTemp = ESPHTTPKonkerUpdate(updateGlobals::httpProtocolGlobal, updateGlobals::manifestEndpointGlobal);
  updateTemp.sendStatusMessage(MSG_FIRMWARE_RECEIVED);
  Log.trace("[UPDT callback] manifest = %X\n", updateTemp.manifest);
  // implicit ~updateTemp() here;
}

void atUpdateEnd()
{
  Log.trace("[UPDT callback] Update done!!!\n");
  ESPHTTPKonkerUpdate::sendFwReceivedMessage();
}

/**
 * Perform update and show the result
 * @param none
 * @return none
 */
void ESPHTTPKonkerUpdate::performUpdate()
{
  WiFiClient wifiClient;
  Log.trace("[UPDT] UPDATING....\n");
  _deviceState = UPDATING;
  String user = _httpProtocol->getUser();
  String password = _httpProtocol->getPassword();

  ESPhttpUpdate.rebootOnUpdate(false);
  ESPhttpUpdate.setLedPin(_STATUS_LED, LOW);
  ESPhttpUpdate.onStart(&atUpdateStart);
  ESPhttpUpdate.onProgress(&atUpdateProgress);
  ESPhttpUpdate.onEnd(&atUpdateEnd);
  updateGlobals::httpProtocolGlobal = _httpProtocol;
  updateGlobals::manifestEndpointGlobal = &_manifestEndpoint;
  Update.setMD5(manifest->getMd5());
  ESPhttpUpdate.setAuthorization(user, password);
  String updateURL = "http://" + _httpProtocol->getHost() + ":" + _httpProtocol->getPort() + "/registry-data" + _fwEndpoint + "/binary";

  Log.trace("[UPDT] Fetching binary from %s\n", updateURL.c_str());
  t_httpUpdate_return ret = ESPhttpUpdate.update(wifiClient, updateURL, String(manifest->getNewVersion()));

  switch(ret)
  {
    case HTTP_UPDATE_FAILED:
      Log.trace("[UPDT] FW update failed. Error = %s\n\n", ESPhttpUpdate.getLastErrorString().c_str());
      this->sendExceptionMessage(EXPT_FW_NOT_FOUND);
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Log.trace("[UPDT] No update\n\n");
      this->sendExceptionMessage(EXPT_FW_NOT_FOUND);
      break;
    case HTTP_UPDATE_OK:
      Log.trace("[UPDT] Complete!\n\n");
      if(manifest->checkChecksum(Update.md5String()))
      {
        this->sendStatusMessage(MSG_CHECKSUM_OK);
        this->addtionalSteps(true);
      }
      else
      {
        this->sendExceptionMessage(EXPT_CHECKSUM_NOK);
        this->addtionalSteps(false);
      }
      Log.trace("[UPDT] restarting device\n\n");
      ESP.restart();
      break;
  }

  delete this->manifest;
}

/**
 * Perform any remaining steps, as sending confirmation messages and
 * saving firmware information to memory
 * @param correct true if update correct so far
 * @return bool
 */
bool ESPHTTPKonkerUpdate::addtionalSteps(bool correct)
{
  if(correct)
  {
    this->sendStatusMessage(MSG_UPDATE_DONE);
    this->sendUpdateConfirmation();
  }

  manifest->applyManifest();
  // TODO set first boot flag
  // TODO save currentFwInfo to memory

  return true;
}

/**
 * Return true if it's the first boot of a new FW and
 * send confirmation that first boot happened after FW update
 * @param none
 * @return bool
 */
bool ESPHTTPKonkerUpdate::checkFirstBoot()
{
  //TODO Code
  // this->sendStatusMessage(MSG_UPDATE_CORRECT)
  return false;
}

/**
 * Validate manifest and populate manifest.newFwInfo
 * @return true if manifest is valid
 */
bool ESPHTTPKonkerUpdate::validateUpdate()
{
  char version[16];

  int ret = manifest->validateManiest();

  if(ret)
  {
    Log.trace("[UPDT] Valid manifest\n", version);
    return true;
  }
  else
  {
    Log.trace("[UPDT] Invalid manifest\n");
    return false;
  }
}

/**
 * Return true if there is a new FW update and parse manifest to json object
 * @return int 0 if failed to parse manifest, 1 if no update, 2 if ok
 */
int ESPHTTPKonkerUpdate::querryPlatform()
{
  String retPayload;

  Log.trace("[UPDT] Checking for updates...\n");

  if(!_httpProtocol->checkConnection())
  {
    Log.trace("[UPDT] No connection to platform\n");
    return 1;
  }

  Log.trace("[UPDT] Checking update at: %s\n", _manifestEndpoint.c_str());

  int retCode = _httpProtocol->request(&retPayload, _manifestEndpoint);

  if(!retCode ||
      retPayload.equals("[]") ||
      retPayload.equals("Resource not found for incoming device"))
  {
    Log.trace("[UPDT] No new FW version\n");
    return 1;
  }
  else
  {
    Log.trace("[UPDT] New version exists\n");

    Log.trace("[UPDT] Payload received = %s\n\n", retPayload.c_str());
    this->manifest = new ManifestHandler();
    if(!this->manifest->startHandler(&currentFwInfo))
    {
      Log.notice("[UPDT] Failed to load current FW info!");
    }
    int begin = retPayload.indexOf("data");
    retPayload.remove(0, begin+6);
    retPayload.remove(retPayload.length()-2);
    if(this->manifest->parseManifest(retPayload.c_str()))
    {
      return 0;
    }
    return 2;
  }

  return retCode;
}

/**
 * Check if there is an update and validade manifest
 * @param callback function*
 * @return bool
 */
bool ESPHTTPKonkerUpdate::checkForUpdate()
{
  if (_last_time_update_check != 0)
  {
    //throtle this call at maximum 1 per minute
    if ((millis() - _last_time_update_check) < 6500)
    {
      //Serial.println("checkForUpdates maximum calls is 1/minute. Please wait more to call again");
      return false;
    }
  }

  int hasManifest = this->querryPlatform();
  if (hasManifest == 0) //[rpi3] get_manifest
  {
    this->sendStatusMessage(MSG_MANIFEST_RECEIVED); // [MJ] this is probably causing some kind of memory overflow and overwriting manifestJson hmmmm
    _last_time_update_check = millis();
    if(this->validateUpdate()) //[rpi3] parse_manifest
    {
      this->sendStatusMessage(MSG_MANIFEST_CORRECT);
      return true;
    }
    this->sendExceptionMessage(EXPT_MANIFEST_INCORRECT);
    delete manifest;
    return false;
  }
  else if(hasManifest == 2)
  {
    this->sendExceptionMessage(EXPT_COULD_NOT_GET_MAN);
    delete manifest;
  }
  // else no new manifest, keep running
  _last_time_update_check = millis();
  return false;
}

/**
 * Send a message to channel _update with current update stage
 * @param msgIndex int corresponding to StatusMessages
 * @return none
 */
void ESPHTTPKonkerUpdate::sendStatusMessage(int msgIndex)
{
  const char * messages[6] =
  {
    "Manifest received",
    "Manifest correct",
    "Firmware received",
    "Checksum OK",
    "Update done",
    "Update correct"
  };

  Log.trace("[UPDT] MESSAGE: %s\n", messages[msgIndex]);

  String smsg=String("{\"update_stage\": \"" + String(messages[msgIndex]) + "\"}");
  int retCode = _httpProtocol->send("_update", smsg);

  Log.trace("[UPDT] Message sent to: _update; Body: %s; httpCode: %d\n", smsg.c_str(), retCode);

  if (retCode == 1)
  {
    Log.trace("[UPDT] Success sending message\n");
  }
  else
  {
    Log.trace("[UPDT] Failed to send message\n");
  }
}

/**
 * Send a message to channel _update with current update exception
 * @param msgIndex int corresponding to StatusMessages
 * @return none
 */
void ESPHTTPKonkerUpdate::sendExceptionMessage(int exptIndex)
{
  const char * messages[6] =
  {
    "Could not get manifest",
    "Manifest incorrect",
    "Did not receive firmware",
    "Checksum NOK",
    "Update failed",
    "New firmware started incorrectly"
  };

  Log.trace("[UPDT] MESSAGE: %s\n", messages[exptIndex]);

  String smsg=String("{\"update_exception\": \"" + String(messages[exptIndex]) + "\"}");
  int retCode = _httpProtocol->send("_update", smsg);

  Log.trace("[UPDT] Exception sent to: _update; Body: %s; httpCode: %d\n", smsg.c_str(), retCode);

  if (retCode == 1)
  {
    Log.trace("[UPDT] Success sending exception\n");
  }
  else
  {
    Log.trace("[UPDT] Failed to send exception\n");
  }
}
