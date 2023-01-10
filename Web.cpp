#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include "ConfigSettings.h"
#include "Web.h"
#include "Utils.h"
#include "SSDP.h"
#include "Somfy.h"

extern ConfigSettings settings;
extern SSDPClass SSDP;
extern rebootDelay_t rebootDelay;
extern SomfyShadeController somfy;
extern Web webServer;

#define WEB_MAX_RESPONSE 2048
static char g_content[WEB_MAX_RESPONSE];

// General responses
static const char _response_404[] = "404: Not Found";

// Encodings
static const char _encoding_text[] = "text/plain";
static const char _encoding_html[] = "text/html";
static const char _encoding_json[] = "application/json";

WebServer server(80);
void Web::startup() {
  Serial.println("Launching web server...");
}
void Web::loop() {
  server.handleClient();
}
void Web::sendCORSHeaders() { 
    //server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
    //server.sendHeader(F("Access-Control-Max-Age"), F("600"));
    //server.sendHeader(F("Access-Control-Allow-Methods"), F("PUT,POST,GET,OPTIONS"));
    //server.sendHeader(F("Access-Control-Allow-Headers"), F("*"));
}
void Web::end() {
  //server.end();
}
void Web::begin() {
    Serial.println("Creating Web MicroServices...");
    server.enableCORS(true);
    server.on("/upnp.xml", []() {
      SSDP.schema(server.client());
    });
    server.on("/", []() {
      webServer.sendCORSHeaders();
      int statusCode = 200;
      // Load the index html page from the data directory.
      Serial.println("Loading file index.html");
      File file = LittleFS.open("/index.html", "r");
      if(!file) {
        Serial.println("Error opening data/index.html");
        server.send(500, _encoding_html, "Unable to open data/index.html");
      }
      server.streamFile(file, _encoding_html);
      file.close();
    });
    server.on("/configRadio", []() {
      webServer.sendCORSHeaders();
      // Load the index html page from the data directory.
      Serial.println("Loading file configRadio.html");
      File file = LittleFS.open("/configRadio.html", "r");
      if(!file) {
        Serial.println("Error opening configRadio.html");
        server.send(500, _encoding_text, "configRadio.html");
      }
      server.streamFile(file, _encoding_html);
      file.close();
    });
    server.on("/main.css", []() {
      webServer.sendCORSHeaders();
      // Load the index html page from the data directory.
      Serial.println("Loading file main.css");
      File file = LittleFS.open("/main.css", "r");
      if(!file) {
        Serial.println("Error opening data/main.css");
        server.send(500, _encoding_text, "Unable to open data/main.css");
      }
      server.streamFile(file, "text/css");
      file.close();
    });
    server.on("/icons.css", []() {
      webServer.sendCORSHeaders();
      // Load the index html page from the data directory.
      Serial.println("Loading file icons.css");
      File file = LittleFS.open("/icons.css", "r");
      if(!file) {
        Serial.println("Error opening data/icons.css");
        server.send(500, _encoding_text, "Unable to open data/icons.css");
      }
      server.streamFile(file, "text/css");
      file.close();
    });
    
    server.onNotFound([]() { server.send(404, _encoding_text, _response_404); });
    server.on("/somfyController", []() {
      webServer.sendCORSHeaders();
      HTTPMethod method = server.method();
      if(method == HTTP_POST || method == HTTP_GET) {
        Serial.println("Begin Serialize Somfy...");
        DynamicJsonDocument doc(2048);
        somfy.toJSON(doc);
        serializeJson(doc, g_content);
        server.send(200, _encoding_json, g_content);
      }
      else server.send(404, _encoding_text, _response_404);
    });
    server.on("/addShade", []() {
      HTTPMethod method = server.method();
      SomfyShade *shade;
      if(method == HTTP_POST || method == HTTP_PUT) {
        Serial.println("Adding a shade");
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if(err) {
          switch(err.code()) {
              case DeserializationError::InvalidInput:
                  server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
                  break;
              case DeserializationError::NoMemory:
                  server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
                  break;
              default:
                  server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
                  break;              
          }
        }
        else {
          Serial.println("Convering to JsonObject");
          JsonObject obj = doc.as<JsonObject>();
          Serial.println("Counting shades");
          if(somfy.shadeCount() > SOMFY_MAX_SHADES) {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of shades exceeded.\"}"));
          }
          else {
            Serial.println("Adding shade");
            shade = somfy.addShade();
            if(shade) {
              Serial.println("Persisting toJSON");
              shade->fromJSON(obj);
              Serial.println("Saving to Preferences");
              shade->save();
              DynamicJsonDocument sdoc(256);
              JsonObject sobj = sdoc.to<JsonObject>();
              shade->toJSON(sobj);
              serializeJson(sdoc, g_content);
              server.send(200, _encoding_json, g_content);
            }
            else {
              server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Max number of shades added.\"}"));
            }
          }
        }
      }
      if(shade) {
        Serial.println("Serializing shade");
        DynamicJsonDocument doc(256);
        JsonObject obj = doc.to<JsonObject>();
        shade->toJSON(obj);
        serializeJson(doc, g_content);
        Serial.println(g_content);
        server.send(200, _encoding_json, g_content);
      }
      else {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error saving Somfy Shade.\"}"));
      }
    });
    server.on("/shade", []() {
      webServer.sendCORSHeaders();
      HTTPMethod method = server.method();
      if(method == HTTP_GET) {
        if(server.hasArg("shadeId")) {
          int shadeId = atoi(server.arg("shadeId").c_str());
          SomfyShade *shade = somfy.getShadeById(shadeId);
          if(shade) {
            DynamicJsonDocument doc(256);
            JsonObject obj = doc.to<JsonObject>();
            shade->toJSON(obj);
            serializeJson(doc, g_content);
            server.send(200, _encoding_json, g_content);
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
        }
        else {
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid shade id.\"}"));
        }
      }
      else if(method == HTTP_PUT || method == HTTP_POST) {
        // We are updating an existing shade.
        if(server.hasArg("plain")) {
          Serial.println("Updating a shade");
          DynamicJsonDocument doc(256);
          DeserializationError err = deserializeJson(doc, server.arg("plain"));
          if(err) {
            switch(err.code()) {
                case DeserializationError::InvalidInput:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
                    break;
                case DeserializationError::NoMemory:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
                    break;
                default:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
                    break;              
            }
          }
          else {
            JsonObject obj = doc.as<JsonObject>();
            if(obj.containsKey("shadeId")) {
              SomfyShade *shade = somfy.getShadeById(obj["shadeId"]);
              if(shade) {
                shade->fromJSON(obj);
                shade->save();
                DynamicJsonDocument sdoc(256);
                JsonObject sobj = sdoc.to<JsonObject>();
                shade->toJSON(sobj);
                serializeJson(sdoc, g_content);
                server.send(200, _encoding_json, g_content);
              }
              else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
            }
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
          }
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
      }
    });
    server.on("/saveShade", []() {
      webServer.sendCORSHeaders();
      HTTPMethod method = server.method();
      if(method == HTTP_PUT || method == HTTP_POST) {
        // We are updating an existing shade.
        if(server.hasArg("plain")) {
          Serial.println("Updating a shade");
          DynamicJsonDocument doc(256);
          DeserializationError err = deserializeJson(doc, server.arg("plain"));
          if(err) {
            switch(err.code()) {
                case DeserializationError::InvalidInput:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
                    break;
                case DeserializationError::NoMemory:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
                    break;
                default:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
                    break;              
            }
          }
          else {
            JsonObject obj = doc.as<JsonObject>();
            if(obj.containsKey("shadeId")) {
              SomfyShade *shade = somfy.getShadeById(obj["shadeId"]);
              if(shade) {
                shade->fromJSON(obj);
                shade->save();
                DynamicJsonDocument sdoc(256);
                JsonObject sobj = sdoc.to<JsonObject>();
                shade->toJSON(sobj);
                serializeJson(sdoc, g_content);
                server.send(200, _encoding_json, g_content);
              }
              else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
            }
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
          }
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
      }      
    });
    server.on("/unlinkRemote", []() {
      webServer.sendCORSHeaders();
      HTTPMethod method = server.method();
      if(method == HTTP_PUT || method == HTTP_POST) {
        // We are updating an existing shade by adding a linked remote.
        if(server.hasArg("plain")) {
          DynamicJsonDocument doc(256);
          DeserializationError err = deserializeJson(doc, server.arg("plain"));
          if(err) {
            switch(err.code()) {
                case DeserializationError::InvalidInput:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
                    break;
                case DeserializationError::NoMemory:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
                    break;
                default:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
                    break;              
            }
          }
          else {
            JsonObject obj = doc.as<JsonObject>();
            if(obj.containsKey("shadeId")) {
              SomfyShade *shade = somfy.getShadeById(obj["shadeId"]);
              if(shade) {
                if(obj.containsKey("remoteAddress")) {
                  shade->unlinkRemote(obj["remoteAddress"]);
                }
                else {
                  server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Remote address not provided.\"}"));
                }
                DynamicJsonDocument sdoc(256);
                JsonObject sobj = sdoc.to<JsonObject>();
                shade->toJSON(sobj);
                serializeJson(sdoc, g_content);
                server.send(200, _encoding_json, g_content);
              }
              else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
            }
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
          }
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No remote object supplied.\"}"));
      }      
    });
    server.on("/linkRemote", []() {
      webServer.sendCORSHeaders();
      HTTPMethod method = server.method();
      if(method == HTTP_PUT || method == HTTP_POST) {
        // We are updating an existing shade by adding a linked remote.
        if(server.hasArg("plain")) {
          Serial.println("Linking a remote");
          DynamicJsonDocument doc(256);
          DeserializationError err = deserializeJson(doc, server.arg("plain"));
          if(err) {
            switch(err.code()) {
                case DeserializationError::InvalidInput:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
                    break;
                case DeserializationError::NoMemory:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
                    break;
                default:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
                    break;              
            }
          }
          else {
            JsonObject obj = doc.as<JsonObject>();
            if(obj.containsKey("shadeId")) {
              SomfyShade *shade = somfy.getShadeById(obj["shadeId"]);
              if(shade) {
                if(obj.containsKey("remoteAddress")) {
                  if(obj.containsKey("rollingCode")) shade->linkRemote(obj["remoteAddress"], obj["rollingCode"]);
                  else shade->linkRemote(obj["remoteAddress"]);
                }
                else {
                  server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Remote address not provided.\"}"));
                }
                DynamicJsonDocument sdoc(256);
                JsonObject sobj = sdoc.to<JsonObject>();
                shade->toJSON(sobj);
                serializeJson(sdoc, g_content);
                server.send(200, _encoding_json, g_content);
              }
              else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade Id not found.\"}"));
            }
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
          }
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No remote object supplied.\"}"));
      }      
    });
    
    server.on("/deleteShade", []() {
      webServer.sendCORSHeaders();
      HTTPMethod method = server.method();
      uint8_t shadeId = 255;
      if(method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
        if(server.hasArg("shadeId")) {
          shadeId = atoi(server.arg("shadeId").c_str());
        }
        else if(server.hasArg("plain")) {
          Serial.println("Deleting a shade");
          DynamicJsonDocument doc(256);
          DeserializationError err = deserializeJson(doc, server.arg("plain"));
          if(err) {
            switch(err.code()) {
                case DeserializationError::InvalidInput:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
                    break;
                case DeserializationError::NoMemory:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
                    break;
                default:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
                    break;              
            }
          }
          else {
            JsonObject obj = doc.as<JsonObject>();
            if(obj.containsKey("shadeId")) shadeId = obj["shadeId"];//obj.getMember("shadeId").as<uint8_t>();
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
          }
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
      }
      SomfyShade *shade = somfy.getShadeById(shadeId);
      if(!shade) server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));
      else {
        somfy.deleteShade(shadeId);
        server.send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Shade deleted.\"}"));
      }
    });
    server.on("/updateFirmware", HTTP_POST, []() {
      webServer.sendCORSHeaders();
      if(Update.hasError())
        server.send(500, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Error updating firmware: \"}");
      else
        server.send(200, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Updating firmware: \"}");
      rebootDelay.reboot = true;
      rebootDelay.rebootTime = millis() + 500;
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Update: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        /* flashing firmware to ESP*/
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
      }      
    });
    server.on("/updateApplication", HTTP_POST, []() {
      webServer.sendCORSHeaders();
      server.sendHeader("Connection", "close");
      server.send(200, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Updating Application: \"}");
      rebootDelay.reboot = true;
      rebootDelay.rebootTime = millis() + 500;
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Update: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) { //start with max available size and tell it we are updating the file system.
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        /* flashing firmware to ESP*/
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
      }      
    });
    server.on("/scanaps", []() {
      webServer.sendCORSHeaders();
      int statusCode = 200;
      int n = WiFi.scanNetworks();
      Serial.print("Scanned ");
      Serial.print(n);
      Serial.println(" networks");
      String content = "{\"connected\": {\"name\":\"" + String(settings.WIFI.ssid) + "\",\"passphrase\":\"" + String(settings.WIFI.passphrase) + "\"}, \"accessPoints\":[";
      for(int i = 0; i < n; ++i) {
        if(i != 0) content += ",";
        content += "{\"name\":\"" + WiFi.SSID(i) + "\",\"channel\":" + WiFi.channel(i) + ",\"encryption\":\"" + settings.WIFI.mapEncryptionType(WiFi.encryptionType(i)) + "\",\"strength\":" + WiFi.RSSI(i) + ",\"macAddress\":\"" + WiFi.BSSIDstr(i) + "\"}";
        delay(10);
      }
      content += "]}";
      server.send(statusCode, "application/json", content);
    });
    server.on("/reboot", []() {
      webServer.sendCORSHeaders();
        HTTPMethod method = server.method();
        if(method == HTTP_POST || method == HTTP_PUT) {
            Serial.println("Rebooting ESP...");
            rebootDelay.reboot = true;
            rebootDelay.rebootTime = millis() + 500;
            server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully started reboot\"}");
        }
        else {
          server.send(201, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
        }
    });
    server.on("/saveRadio", []() {
      webServer.sendCORSHeaders();
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if(err) {
        Serial.print("Error parsing JSON ");
        Serial.println(err.c_str());
        String msg = err.c_str();
        server.send(400, _encoding_html, "Error parsing JSON body<br>" + msg);
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        HTTPMethod method = server.method();
        if(method == HTTP_POST || method == HTTP_PUT) {
          somfy.transceiver.fromJSON(obj);
          somfy.transceiver.save();
          DynamicJsonDocument sdoc(512);
          JsonObject sobj = sdoc.to<JsonObject>();
          somfy.transceiver.toJSON(sobj);
          serializeJson(sdoc, g_content);
          server.send(200, _encoding_json, g_content);
          //server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully saved radio\"}");
        }
        else {
          server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
        }
      }
    });
    server.on("/getRadio", []() {
      webServer.sendCORSHeaders();
      DynamicJsonDocument doc(1024);
      JsonObject obj = doc.to<JsonObject>();
      somfy.transceiver.toJSON(obj);
      serializeJson(doc, g_content);
      server.send(200, _encoding_json, g_content);
    });

    /*  THE Following endpoints have been deprecated now that I now can tune the radio settings without hacking around.  The relevant settings
     *  have been included when the transceiver is saved.
    server.on("/setRadioConfig", []() {
      webServer.sendCORSHeaders();
      DynamicJsonDocument doc(1280);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if(err) {
        Serial.print("Error parsing JSON ");
        Serial.println(err.c_str());
        String msg = err.c_str();
        server.send(400, _encoding_html, "Error parsing JSON body<br>" + msg);
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        transceiver_config_t cfg;
        cfg.fromJSON(obj);
        cfg.save();
        cfg.apply();
        serializeJson(doc, g_content);
        server.send(200, _encoding_json, g_content);
      }
    });
    server.on("/getRadioConfig", []() {
      webServer.sendCORSHeaders();
      DynamicJsonDocument doc(1024);
      JsonObject obj = doc.to<JsonObject>();
      transceiver_config_t cfg;
      cfg.load();
      cfg.toJSON(obj);
      serializeJson(doc, g_content);
      server.send(200, _encoding_json, g_content);
    });
    */
    server.on("/sendShadeCommand", [](){
     server.sendHeader("Access-Control-Allow-Origin", "*");
     HTTPMethod method = server.method();
      uint8_t shadeId = 255;
      somfy_commands command = somfy_commands::My;
      if(method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
        if(server.hasArg("shadeId")) {
          shadeId = atoi(server.arg("shadeId").c_str());
          if(server.hasArg("command")) command = translateSomfyCommand(server.arg("command"));
        }
        else if(server.hasArg("plain")) {
          Serial.println("Sending Shade Command");
          DynamicJsonDocument doc(256);
          DeserializationError err = deserializeJson(doc, server.arg("plain"));
          if(err) {
            switch(err.code()) {
                case DeserializationError::InvalidInput:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
                    break;
                case DeserializationError::NoMemory:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
                    break;
                default:
                    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
                    break;              
            }
          }
          else {
            JsonObject obj = doc.as<JsonObject>();
            if(obj.containsKey("shadeId")) shadeId = obj["shadeId"];
            else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
            if(obj.containsKey("command")) {
              String scmd = obj["command"];
              command = translateSomfyCommand(scmd);
            }
          }
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
      }
      SomfyShade *shade = somfy.getShadeById(shadeId);
      if(shade) {
        // Send the command to the shade.
        shade->sendCommand(command);
        DynamicJsonDocument sdoc(256);
        JsonObject sobj = sdoc.to<JsonObject>();
        shade->toJSON(sobj);
        serializeJson(sdoc, g_content);
        server.send(200, _encoding_json, g_content);
      }
      else {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade with the specified id not found.\"}"));        
      }
    });
    server.on("/setgeneral", []() {
      webServer.sendCORSHeaders();
      int statusCode = 200;
      DynamicJsonDocument doc(256);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if(err) {
        Serial.print("Error parsing JSON ");
        Serial.println(err.c_str());
        String msg = err.c_str();
        server.send(400, _encoding_html, "Error parsing JSON body<br>" + msg);
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        HTTPMethod method = server.method();
        if(method == HTTP_POST || method == HTTP_PUT) {
          // Parse out all the inputs.
          if(obj.containsKey("hostname")) {
            settings.WIFI.fromJSON(obj);
            settings.WIFI.save();
          }
          if(obj.containsKey("ntpServer") || obj.containsKey("ntpServer")) {
            settings.NTP.fromJSON(obj);
            settings.NTP.save();
          }
          server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set General Settings\"}");
        }
        else {
          server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
        }
      }
    });
    server.on("/connectwifi", []() { 
      webServer.sendCORSHeaders();
      int statusCode = 200;
      Serial.println("Settings WIFI connection...");
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if(err) {
        Serial.print("Error parsing JSON ");
        Serial.println(err.c_str());
        String msg = err.c_str();
        server.send(400, "text/html", "Error parsing JSON body<br>" + msg);
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        HTTPMethod method = server.method();
        //Serial.print(F("HTTP Method: "));
        //Serial.println(server.method());
        if(method == HTTP_POST || method == HTTP_PUT) {
          String ssid = "";
          String passphrase = "";
          if(obj.containsKey("ssid")) ssid = obj["ssid"].as<String>();
          if(obj.containsKey("passphrase")) passphrase = obj["passphrase"].as<String>();    
          bool reboot;
          if(ssid.compareTo(settings.WIFI.ssid) != 0) reboot = true;
          if(passphrase.compareTo(settings.WIFI.passphrase) != 0) reboot = true;
          if(!settings.WIFI.ssidExists(ssid.c_str())) {
            server.send(400, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"WiFi Network Does not exist\"}");
          }
          else {
            SETCHARPROP(settings.WIFI.ssid, ssid.c_str(), sizeof(settings.WIFI.ssid));
            SETCHARPROP(settings.WIFI.passphrase, passphrase.c_str(), sizeof(settings.WIFI.passphrase));
            if(obj.containsKey("ssdpBroadcast")) settings.WIFI.ssdpBroadcast = obj["ssdpBroadcast"].as<bool>();
            settings.WIFI.save();
            settings.WIFI.print();
            server.send(201, _encoding_json, "{\"status\":\"OK\",\"desc\":\"Successfully set server connection\"}");
            if(reboot) {
              Serial.println("Rebooting ESP for new WiFi settings...");
              rebootDelay.reboot = true;
              rebootDelay.rebootTime = millis() + 1000;
            }
          }
        }
        else {
          server.send(201, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
        }
      }
    });
    server.on("/modulesettings", []() {
      webServer.sendCORSHeaders();
      DynamicJsonDocument doc(512);
      JsonObject obj = doc.to<JsonObject>();
      doc["fwVersion"] = settings.fwVersion;
      settings.WIFI.toJSON(obj);
      settings.NTP.toJSON(obj);
      serializeJson(doc, g_content);
      server.send(200, _encoding_json, g_content);
    });    
    server.on("/connectmqtt", []() { 
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if(err) {
        Serial.print("Error parsing JSON ");
        Serial.println(err.c_str());
        String msg = err.c_str();
        server.send(400, F("text/html"), "Error parsing JSON body<br>" + msg);
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        HTTPMethod method = server.method();
        Serial.print(F("HTTP Method: "));
        Serial.println(server.method());
        if(method == HTTP_POST || method == HTTP_PUT) {
          settings.MQTT.fromJSON(obj);
          settings.MQTT.save();
          StaticJsonDocument<512> sdoc;
          JsonObject sobj = sdoc.to<JsonObject>();
          settings.MQTT.toJSON(sobj);
          serializeJson(sdoc, g_content);
          server.send(200, _encoding_json, g_content);
        }
        else {
          server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
        }
      }
    });
    server.on("/mqttsettings", []() {
      webServer.sendCORSHeaders();
      DynamicJsonDocument doc(512);
      JsonObject obj = doc.to<JsonObject>();
      settings.MQTT.toJSON(obj);
      serializeJson(doc, g_content);
      server.send(200, _encoding_json, g_content);
    });
    server.begin();
}
