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
void GitRelease::setProperty(const char *key, const char *val) {
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
    //Serial.println(this->releaseDate);
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
  JsonObject ver = obj.createNestedObject("version");
  this->version.toJSON(ver);
  return true;
}
#define ERR_CLIENT_OFFSET -50

int16_t GitRepo::getReleases(uint8_t num) {
  WiFiClientSecure *client = new WiFiClientSecure;
  if(client) {
    client->setInsecure();
    HTTPClient https;
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
    if(https.begin(*client, url)) {
      int httpCode = https.GET();
      Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
      if(httpCode > 0) {
        int len = https.getSize();
        Serial.printf("[HTTPS] GET... code: %d - %d\n", httpCode, len);
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          WiFiClient *stream = https.getStreamPtr();
          uint8_t buff[128] = {0};
          char jsonElem[32] = "";
          char jsonValue[128] = "";
          int arrTok = 0;
          int objTok = 0;
          bool inQuote = false;
          bool inElem = false;
          bool inValue = false;
          bool awaitValue = false;
          while(https.connected() && (len > 0 || len == -1) && ndx < count) {
            size_t size = stream->available();
            if(size) {
              int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
              //Serial.write(buff, c);
              if(len > 0) len -= c;
              // Now we should have some data.
              for(uint8_t i = 0; i < c; i++) {
                char ch = static_cast<char>(buff[i]);
                if(ch == '[') arrTok++;
                else if(ch == ']') arrTok--;
                else if(ch == '{') {
                  objTok++;
                  if(objTok != 1) inElem = inValue = awaitValue = false;
                }
                else if(ch == '}') {
                  objTok--;
                  if(objTok == 0) ndx++;
                }
                else if(objTok == 1) {
                  // We only want data from the root object.
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
                      this->releases[ndx].setProperty(jsonElem, jsonValue);
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
                      this->releases[ndx].setProperty(jsonElem, jsonValue);
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
        else return httpCode;
      }
      https.end();  
    }
    delete client;
  }
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
    if(this->lastCheck + 14400000 < millis()) { // 4 hours
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
    this->status = GIT_UPDATE_CANCELLED;
    this->emitUpdateCheck();
    this->cancelled = true;
  }
}
void GitUpdater::checkForUpdate() {
  if(this->status != 0) return; // If we are already checking.
  this->status = GIT_STATUS_CHECK;
  GitRepo repo;
  this->lastCheck = millis();
  this->updateAvailable = false;
  this->error = repo.getReleases(2);
  if(this->error == 0) { // Get 2 releases so we can filter our pre-releases
    this->setCurrentRelease(repo);
  }
  else {
    this->emitUpdateCheck();
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
void GitUpdater::emitDownloadProgress(size_t total, size_t loaded, const char *evt) { this->emitDownloadProgress(255, total, loaded, evt); }
void GitUpdater::emitDownloadProgress(uint8_t num, size_t total, size_t loaded, const char *evt) {
  char buf[420];
  snprintf(buf, sizeof(buf), "{\"ver\":\"%s\",\"part\":%d,\"file\":\"%s\",\"total\":%d,\"loaded\":%d, \"error\":%d}", this->targetRelease, this->partition, this->currentFile, total, loaded, this->error);
  if(num >= 255) sockEmit.sendToClients(evt, buf);
  else sockEmit.sendToClient(num, evt, buf);
  sockEmit.loop();
  webServer.loop();
}


bool GitUpdater::beginUpdate(const char *version) {
  Serial.println("Begin update called...");
  if(strcmp(version, "Main") == 0)  strcpy(this->baseUrl, "https://raw.githubusercontent.com/rstrouse/ESPSomfy-RTS/master/");
  else sprintf(this->baseUrl, "https://github.com/rstrouse/ESPSomfy-RTS/releases/download/%s/", version);
  
  strcpy(this->targetRelease, version);
  this->emitUpdateCheck();
  strcpy(this->currentFile, "SomfyController.ino.esp32.bin");
  this->partition = U_FLASH;
  this->cancelled = false;
  this->error = 0;
  this->error = this->downloadFile();
  if(this->error == 0 && !this->cancelled) {
    strcpy(this->currentFile, "SomfyController.littlefs.bin");
    this->partition = U_SPIFFS;
    this->error = this->downloadFile();
    if(this->error == 0) {
      settings.fwVersion.parse(version);
      somfy.commit();
      rebootDelay.reboot = true;
      rebootDelay.rebootTime = millis() + 500;
    }
  }
  this->status = GIT_UPDATE_COMPLETE;
  this->emitUpdateCheck();
  return true;
}
bool GitUpdater::endUpdate() { return true; }
int8_t GitUpdater::downloadFile() {
  WiFiClientSecure *client = new WiFiClientSecure;
  Serial.printf("Begin update %s\n", this->currentFile);
  if(client) {
    client->setInsecure();
    HTTPClient https;
    uint8_t ndx = 0;
    char url[128];
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
            Update.printError(Serial);
            https.end();
            return -(Update.getError() + UPDATE_ERR_OFFSET);
          }
          uint8_t *buff = (uint8_t *)malloc(MAX_BUFF_SIZE);
          if(buff) {
            this->emitDownloadProgress(len, total);
            while(https.connected() && (len > 0 || len == -1) && total < len) {
              size_t size = stream->available();
              if(size) {
                if(this->cancelled) {
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
                  if(!Update.end()) {
                    Update.printError(Serial);
                  }
                  else {
                    
                  }
                  https.end();
                }
              }
            }
            
            Serial.printf("Update %s complete\n", this->currentFile);
            
            free(buff);
          }
          else {
            // TODO: memory allocation error.
          }
        }
        else {
          return httpCode;
        }
      }        
      https.end();  
      Serial.printf("End update %s\n", this->currentFile);

    }
    delete client;
  }
  return 0;
}
