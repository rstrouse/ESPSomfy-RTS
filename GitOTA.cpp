#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <HTTPClient.h>
#include "GitOTA.h"
#include "Utils.h"
#include "ConfigSettings.h"
#include "Sockets.h"
#include "Somfy.h"
#include "Web.h"


extern ConfigSettings settings;
extern SocketEmitter sockEmit;
extern SomfyShadeController somfy;
extern rebootDelay_t rebootDelay;
extern Web webServer;


#define MAX_BUFF_SIZE 4096
void GitRelease::setReleaseProperty(const char *key, const char *val) {
  if(strcmp(key, "id") == 0) this->id = atol(val);
  else if(strcmp(key, "draft") == 0) this->draft = toBoolean(val, false);
  else if(strcmp(key, "prerelease") == 0) this->preRelease = toBoolean(val, false);
  else if(strcmp(key, "name") == 0) strlcpy(this->name, val, sizeof(this->name));
  else if(strcmp(key, "tag_name") == 0) {
    this->version.parse(val);
  }
  else if(strcmp(key, "published_at") == 0) {
    //Serial.printf("Key:[%s] Value:[%s]\n", key, val);
    this->releaseDate = Timestamp::parseUTCTime(val);
  }
}
void GitRelease::setAssetProperty(const char *key, const char *val) {
  if(strcmp(key, "name") == 0) {
    //Serial.println(val);
    if(strstr(val, "littlefs.bin")) this->hasFS = true;
    else if(strstr(val, "ino.esp32.bin")) {
      if(strlen(this->hwVersions)) strcat(this->hwVersions, ",");
      strcat(this->hwVersions, "32");
    }
    else if(strstr(val, "ino.esp32s3.bin")) {
      if(strlen(this->hwVersions)) strcat(this->hwVersions, ",");
      strcat(this->hwVersions, "s3");
    }
    else if(strstr(val, "ino.esp32s2.bin")) {
      if(strlen(this->hwVersions)) strcat(this->hwVersions, ",");
      strcat(this->hwVersions, "s2");
    }
    else if(strstr(val, "ino.esp32c3.bin")) {
      if(strlen(this->hwVersions)) strcat(this->hwVersions, ",");
      strcat(this->hwVersions, "c3");
    }
    else if(strstr(val, "ino.esp32c2.bin")) {
      if(strlen(this->hwVersions)) strcat(this->hwVersions, ",");
      strcat(this->hwVersions, "c2");
    }
    else if(strstr(val, "ino.esp32c6.bin")) {
      if(strlen(this->hwVersions)) strcat(this->hwVersions, ",");
      strcat(this->hwVersions, "c6");
    }
    else if(strstr(val, "ino.esp32h2.bin")) {
      if(strlen(this->hwVersions)) strcat(this->hwVersions, ",");
      strcat(this->hwVersions, "h2");
    }
  }
}
bool GitRelease::toJSON(JsonObject &obj) {
  Timestamp ts;
  obj["id"] = this->id;
  obj["name"] = this->name;
  obj["date"] = ts.getISOTime(this->releaseDate);
  obj["draft"] = this->draft;
  obj["preRelease"] = this->preRelease;
  obj["main"] = this->main;
  obj["hasFS"] = this->hasFS;
  obj["hwVersions"] = this->hwVersions;
  JsonObject ver = obj.createNestedObject("version");
  this->version.toJSON(ver);
  return true;
}
#define ERR_CLIENT_OFFSET -50

int16_t GitRepo::getReleases(uint8_t num) {
  WiFiClientSecure sclient;
  sclient.setInsecure();
  sclient.setHandshakeTimeout(3);
  uint8_t ndx = 0;
  uint8_t count = min((uint8_t)GIT_MAX_RELEASES, num);
  char url[128];
  memset(this->releases, 0x00, sizeof(GitRelease) * GIT_MAX_RELEASES);
  sprintf(url, "https://api.github.com/repos/rstrouse/espsomfy-rts/releases?per_page=%d&page=1", count);
  GitRelease *main = &this->releases[GIT_MAX_RELEASES];
  main->releaseDate = Timestamp::now();
  main->id = 1;
  main->main = true;
  strcpy(main->version.name, "main");
  strcpy(main->name, "Main");
  strcpy(main->hwVersions, "32,s3");
  HTTPClient *https = new HTTPClient();
  https->setReuse(false);
  if(https->begin(sclient, url)) {
    int httpCode = https->GET();
    Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
    if(httpCode > 0) {
      int len = https->getSize();
      Serial.printf("[HTTPS] GET... code: %d - %d\n", httpCode, len);
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        WiFiClient *stream = https->getStreamPtr();
        uint8_t buff[128] = {0};
        char jsonElem[32] = "";
        char jsonValue[128] = "";
        int arrTok = 0;
        int objTok = 0;
        bool inQuote = false;
        bool inElem = false;
        bool inValue = false;
        bool awaitValue = false;
        bool inAss = false;
        while(https->connected() && (len > 0 || len == -1) && ndx < count) {
          size_t size = stream->available();
          if(size) {
            int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
            //Serial.write(buff, c);
            if(len > 0) len -= c;
            // Now we should have some data.
            for(uint8_t i = 0; i < c; i++) {
              // Read the buffer a byte at a time until we have a key value pair.
              char ch = static_cast<char>(buff[i]);
              if(ch == '[') {
                arrTok++;
                if(arrTok == 2 && strcmp(jsonElem, "assets") == 0) {
                  inElem = inValue = awaitValue = false;
                  inAss = true;
                  //Serial.printf("%s: %d\n", jsonElem, arrTok);
                }
                else if(arrTok < 2) inAss = false;
              }
              else if(ch == ']') {
                arrTok--;
                if(arrTok < 2) inAss = false;
              }
              else if(ch == '{') {
                objTok++;
                if(objTok != 1 && !inAss) inElem = inValue = awaitValue = false;
              }
              else if(ch == '}') {
                objTok--;
                if(objTok == 0) ndx++;
              }
              else if(objTok == 1 || inAss) {
                // We only want data from the root object.
                //if(inAss) Serial.print(ch);
                if(ch == '\"') {
                  inQuote = !inQuote;
                  if(inElem) {
                    inElem = false;
                    awaitValue = true;
                  }
                  else if(inValue) {
                    inValue = false;
                    inElem = false;
                    awaitValue = false;
                    if(inAss)
                      this->releases[ndx].setAssetProperty(jsonElem, jsonValue);
                    else
                      this->releases[ndx].setReleaseProperty(jsonElem, jsonValue);
                    memset(jsonElem, 0x00, sizeof(jsonElem));
                    memset(jsonValue, 0x00, sizeof(jsonValue));
                  }
                  else if(awaitValue) inValue = true;
                  else {
                    inElem = true;
                    awaitValue = false;
                  }
                }
                else if(awaitValue) {
                  if(ch != ' ' && ch != ':') {
                    strncat(jsonValue, &ch, 1);
                    awaitValue = false;
                    inValue = true;
                  }
                }
                else if((!inQuote && ch == ',') || ch == '\r' || ch == '\n') {
                  inElem = inValue = awaitValue = false;
                  if(strlen(jsonElem) > 0) {
                    if(inAss)
                      this->releases[ndx].setAssetProperty(jsonElem, jsonValue);
                    else
                      this->releases[ndx].setReleaseProperty(jsonElem, jsonValue);
                  }
                  memset(jsonElem, 0x00, sizeof(jsonElem));
                  memset(jsonValue, 0x00, sizeof(jsonValue));
                }
                else {
                  if(inElem) {
                    if(strlen(jsonElem) < sizeof(jsonElem) - 1) strncat(jsonElem, &ch, 1);
                  }
                  else if(inValue) {
                    if(strlen(jsonValue) < sizeof(jsonValue) - 1) strncat(jsonValue, &ch, 1);
                  }
                }
              }
            }
            delay(1);
          }
          //else break;
        }
      }
      else {
        https->end();
        sclient.stop();
        delete https;
        return httpCode;
      }
    }
    https->end();  
    delete https;
  }
  sclient.stop();
  settings.printAvailHeap();
  return 0;
}
bool GitRepo::toJSON(JsonObject &obj) {
  JsonObject fw = obj.createNestedObject("fwVersion");
  settings.fwVersion.toJSON(fw);
  JsonObject app = obj.createNestedObject("appVersion");
  settings.appVersion.toJSON(app);
  JsonArray arr = obj.createNestedArray("releases");
  for(uint8_t i = 0; i < GIT_MAX_RELEASES + 1; i++) {
    if(this->releases[i].id == 0) continue;
    JsonObject o = arr.createNestedObject();
    this->releases[i].toJSON(o);
  }
  return true;
}
#define UPDATE_ERR_OFFSET 20
#define ERR_DOWNLOAD_HTTP -40
#define ERR_DOWNLOAD_BUFFER -41
#define ERR_DOWNLOAD_CONNECTION -42

void GitUpdater::loop() {
  if(this->status == GIT_STATUS_READY) {
    //if(this->lastCheck == 0) 
      //this->lastCheck = millis();
    //else 
    if(settings.checkForUpdate && 
      //(this->lastCheck + 14400000 < millis() || this->lastCheck == 0) && !rebootDelay.reboot) { // 4 hours
      (this->lastCheck + 86400000 < millis() || this->lastCheck == 0) && !rebootDelay.reboot) { // 1 day
      this->checkForUpdate();
    }
  }
  else if(this->status == GIT_AWAITING_UPDATE) {
    Serial.println("Starting update process.........");
    this->status = GIT_UPDATING;
    this->beginUpdate(this->targetRelease);
    this->status = GIT_STATUS_READY;
    this->emitUpdateCheck();
  }
  else if(this->status == GIT_UPDATE_CANCELLING) {
    Serial.println("Cancelling update process..........");
    if(!this->lockFS) {
      this->status = GIT_UPDATE_CANCELLED;
      this->cancelled = true;
      this->emitUpdateCheck();
    }
  }
}
void GitUpdater::checkForUpdate() {
  if(this->status != 0) return; // If we are already checking.
  Serial.println("Check github for updates...");
  this->status = GIT_STATUS_CHECK;
  settings.printAvailHeap();  
  this->lastCheck = millis();
  if(this->checkInternet() == 0) {
    GitRepo repo;
    this->updateAvailable = false;
    this->error = repo.getReleases(2);
    if(this->error == 0) { // Get 2 releases so we can filter our pre-releases
      this->setCurrentRelease(repo);
    }
    else {
      this->emitUpdateCheck();
    }
  }
  this->status = GIT_STATUS_READY;
}
void GitUpdater::setCurrentRelease(GitRepo &repo) {
  this->updateAvailable = false;
  for(uint8_t i = 0; i < 2; i++) {
    if(repo.releases[i].draft || repo.releases[i].preRelease || repo.releases[i].id == 0) continue;
    // Compare the versions.  
    this->latest.copy(repo.releases[i].version);
    if(repo.releases[i].version.compare(settings.fwVersion) > 0) {
      // We have a new release.
      this->updateAvailable = true;
    }
    break;
  }
  this->emitUpdateCheck();
}
void GitUpdater::toJSON(JsonObject &obj) {
  obj["available"] = this->updateAvailable;
  obj["status"] = this->status;
  obj["error"] = this->error;
  obj["cancelled"] = this->cancelled;
  obj["checkForUpdate"] = settings.checkForUpdate;
  obj["inetAvailable"] = this->inetAvailable;
  JsonObject fw = obj.createNestedObject("fwVersion");
  settings.fwVersion.toJSON(fw);
  JsonObject app = obj.createNestedObject("appVersion");
  settings.appVersion.toJSON(app);
  JsonObject latest = obj.createNestedObject("latest");
  this->latest.toJSON(latest);
}
void GitUpdater::emitUpdateCheck(uint8_t num) {
  ClientSocketEvent evt("fwStatus");
  DynamicJsonDocument doc(512);
  JsonObject obj = doc.to<JsonObject>();
  this->toJSON(obj);
  if(num == 255)
    sockEmit.sendToClients("fwStatus", doc);
  else
    sockEmit.sendToClient(num, "fwStatus", doc);
}
int GitUpdater::checkInternet() {
  int err = 500;
  uint32_t t = millis();
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(3);
  HTTPClient *https = new HTTPClient();
  https->setReuse(false);
  if(https->begin(client, "https://github.com/rstrouse/ESPSomfy-RTS")) {
    https->setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    https->setTimeout(5000);
    int httpCode = https->sendRequest("HEAD");
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) {
      err = 0;
      Serial.printf("Internet is Available: %ldms\n", millis() - t);
      this->inetAvailable = true;
    }
    else {
      err = httpCode;
      Serial.printf("Internet is Unavailable: %d: %ldms\n", err, millis() - t);
      this->inetAvailable = false;
    }
    https->end();
  }
  client.stop();
  delete https;
  return err;
}
void GitUpdater::emitDownloadProgress(size_t total, size_t loaded, const char *evt) { this->emitDownloadProgress(255, total, loaded, evt); }
void GitUpdater::emitDownloadProgress(uint8_t num, size_t total, size_t loaded, const char *evt) {
  char buf[420];
  snprintf(buf, sizeof(buf), "{\"ver\":\"%s\",\"part\":%d,\"file\":\"%s\",\"total\":%d,\"loaded\":%d, \"error\":%d}", this->targetRelease, this->partition, this->currentFile, total, loaded, this->error);
  if(num >= 255) sockEmit.sendToClients(evt, buf);
  else sockEmit.sendToClient(num, evt, buf);
  sockEmit.loop();
  webServer.loop();
}
void GitUpdater::setFirmwareFile() {
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    switch(ci.model) {
      case esp_chip_model_t::CHIP_ESP32S3:
        strcpy(this->currentFile, "SomfyController.ino.esp32s3.bin");
        break;
      case esp_chip_model_t::CHIP_ESP32S2:
        strcpy(this->currentFile, "SomfyController.ino.esp32s2.bin");
        break;
      case esp_chip_model_t::CHIP_ESP32C3:
        strcpy(this->currentFile, "SomfyController.ino.esp32c3.bin");
        break;
      default:
        strcpy(this->currentFile, "SomfyController.ino.esp32.bin");
        break;
    }
}

bool GitUpdater::beginUpdate(const char *version) {
  Serial.println("Begin update called...");
  if(strcmp(version, "Main") == 0)  strcpy(this->baseUrl, "https://raw.githubusercontent.com/rstrouse/ESPSomfy-RTS/master/");
  else sprintf(this->baseUrl, "https://github.com/rstrouse/ESPSomfy-RTS/releases/download/%s/", version);
  
  strcpy(this->targetRelease, version);
  this->emitUpdateCheck();
  this->setFirmwareFile();
  this->partition = U_FLASH;
  this->lockFS = this->cancelled = false;
  this->error = 0;
  this->error = this->downloadFile();
  if(this->error == 0 && !this->cancelled) {
    somfy.commit();
    strcpy(this->currentFile, "SomfyController.littlefs.bin");
    this->partition = U_SPIFFS;
    this->lockFS = true;
    this->error = this->downloadFile();
    this->lockFS = false;
    if(this->error == 0) {
      settings.fwVersion.parse(version);
      delay(100);
      Serial.println("Committing Configuration...");
      somfy.commit();
    }
    rebootDelay.reboot = true;
    rebootDelay.rebootTime = millis() + 500;
  }
  this->status = GIT_UPDATE_COMPLETE;
  this->emitUpdateCheck();
  return true;
}
bool GitUpdater::recoverFilesystem() {
  sprintf(this->baseUrl, "https://github.com/rstrouse/ESPSomfy-RTS/releases/download/%s/", settings.fwVersion.name);
  strcpy(this->currentFile, "SomfyController.littlefs.bin");
  this->status = GIT_UPDATING;
  this->partition = U_SPIFFS;
  this->lockFS = true;
  this->error = this->downloadFile();
  this->lockFS = false;
  if(this->error == 0) {
    delay(100);
    Serial.println("Committing Configuration...");
    somfy.commit();
  }
  this->status = GIT_UPDATE_COMPLETE;
  rebootDelay.reboot = true;
  rebootDelay.rebootTime = millis() + 500;
  return true;
}
bool GitUpdater::endUpdate() { return true; }
int8_t GitUpdater::downloadFile() {
  WiFiClientSecure *client = new WiFiClientSecure;
  Serial.printf("Begin update %s\n", this->currentFile);
  if(client) {
    client->setInsecure();
    HTTPClient https;
    char url[196];
    sprintf(url, "%s%s", this->baseUrl, this->currentFile);
    Serial.println(url);
    if(https.begin(*client, url)) {
      https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
      Serial.print("[HTTPS] GET...\n");
      int httpCode = https.GET();
      if(httpCode > 0) {
        size_t len = https.getSize();
        size_t total = 0;
        uint8_t pct = 0;
        Serial.printf("[HTTPS] GET... code: %d - %d\n", httpCode, len);
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) {
          WiFiClient *stream = https.getStreamPtr();
          if(!Update.begin(len, this->partition)) {
            Serial.println("Update Error detected!!!!!");
            Update.printError(Serial);
            https.end();
            return -(Update.getError() + UPDATE_ERR_OFFSET);
          }
          uint8_t *buff = (uint8_t *)malloc(MAX_BUFF_SIZE);
          if(buff) {
            this->emitDownloadProgress(len, total);
            int timeouts = 0;
            while(https.connected() && (len > 0 || len == -1) && total < len) {
              size_t size = stream->available();
              if(size) {
                if(this->cancelled && !this->lockFS) {
                  Update.abort();
                  https.end();
                  free(buff);
                  return -(Update.getError() + UPDATE_ERR_OFFSET);
                }
                int c = stream->readBytes(buff, ((size > MAX_BUFF_SIZE) ? MAX_BUFF_SIZE : size));
                total += c;
                //Serial.println(total);
                if (Update.write(buff, c) != c) {
                  Update.printError(Serial);
                  Serial.printf("Upload of %s aborted invalid size %d\n", url, c);
                  free(buff);
                  https.end();
                  return -(Update.getError() + UPDATE_ERR_OFFSET);
                }
                // Calculate the percentage.
                uint8_t p = (uint8_t)floor(((float)total / (float)len) * 100.0f);
                if(p != pct) {
                  pct = p;
                  Serial.printf("LEN:%d TOTAL:%d %d%%\n", len, total, pct);
                  this->emitDownloadProgress(len, total);
                }
                delay(1);
                if(total >= len) {
                  if(!Update.end(true)) {
                    Serial.println("Error downloading update...");
                    Update.printError(Serial);
                  }
                  else {
                    Serial.println("Update.end Called...");
                  }
                  https.end();
                }
              }
              else {
                timeouts++;
                if(timeouts >= 500) {
                  Update.abort();
                  https.end();
                  free(buff);
                  Serial.println("Stream timeout!!!");
                  return -43;
                }
                sockEmit.loop();
                webServer.loop();
                delay(100);
              }
            }
            free(buff);
            if(len > total) {
              Update.abort();
              somfy.commit();
              Serial.println("Error downloading file!!!");
              return -42;
              
            }
            else
              Serial.printf("Update %s complete\n", this->currentFile);
            
          }
          else {
            // TODO: memory allocation error.
            Serial.println("Unable to allocate memory for update!!!");
          }
        }
        else {
          Serial.printf("Invalid HTTP Code... %d", httpCode);
          return httpCode;
        }
      }        
      else {
        Serial.printf("Invalid HTTP Code: %d\n", httpCode);
      }
      
      if(https.connected()) https.end();  
      Serial.printf("End update %s\n", this->currentFile);

    }
    client->stop();
    delete client;
  }
  return 0;
}
