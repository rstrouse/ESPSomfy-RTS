#include <Arduino.h>
#include <time.h>
#include "Utils.h"


/*********************************************************************
 * Timestamp class members
 ********************************************************************/
unsigned long Timestamp::epoch() {
  struct tm tmNow;
  time_t now;
  if(!getLocalTime(&tmNow,50)) return 0;
  time(&now);
  return now;
}
time_t Timestamp::now() {
  struct tm tmNow;
  getLocalTime(&tmNow,50);
  return mktime(&tmNow);
}
time_t Timestamp::getUTC() { 
  time_t t;
  time(&t);
  return t; 
}
time_t Timestamp::mkUTCTime(struct tm *dt) {
  time_t tsBadLocal = mktime(dt);

  struct tm tmUTC;
  struct tm tmLocal;
  gmtime_r(&tsBadLocal, &tmUTC);
  localtime_r(&tsBadLocal, &tmLocal);
  time_t tsBadUTC = mktime(&tmUTC);
  time_t tsLocal = mktime(&tmLocal);
  time_t tsLocalOffset = tsLocal - tsBadUTC;
  return tsBadLocal + tsLocalOffset;
}
time_t Timestamp::parseUTCTime(const char *buff) {
  struct tm dt;
  dt.tm_hour = 0;
  dt.tm_mday = 0;
  dt.tm_mon = 0;
  dt.tm_year = 0;
  dt.tm_wday = 0;
  dt.tm_yday = 0;
  dt.tm_isdst = false;
  char num[5];
  uint8_t i = 0;
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 5 && i < strlen(buff);) {
    char ch = buff[i++];
    if(ch == '-') break;
    if(!isdigit(ch)) continue;
    else num[j++] = ch;
  }
  dt.tm_year = atoi(num)-1900;
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 5 && i < strlen(buff);) {
    char ch = buff[i++];
    if(ch == '-') break;
    if(!isdigit(ch)) continue;
    else num[j++] = ch;
  }
  dt.tm_mon = atoi(num)-1;  
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 5 && i < strlen(buff);) {
    char ch = buff[i++];
    if(ch == '-' || ch == 'T' || ch == 't') break;
    if(!isdigit(ch)) continue;
    else num[j++] = ch;
  }
  dt.tm_mday = atoi(num);
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 5 && i < strlen(buff);) {
    char ch = buff[i++];
    if(ch == '-' || ch == ':') break;
    if(!isdigit(ch)) continue;
    else num[j++] = ch;
  }
  dt.tm_hour = atoi(num);
  memset(num, 0x00, sizeof(num));
  for(uint8_t j = 0; j < 5 && i < strlen(buff);) {
    char ch = buff[i++];
    if(ch == '-' || ch == ':') break;
    if(!isdigit(ch)) continue;
    else num[j++] = ch;
  }
  dt.tm_min = atoi(num);
  for(uint8_t j = 0; j < 5 && i < strlen(buff);) {
    char ch = buff[i++];
    if(ch == '-' || ch == ':' || ch == 'Z') break;
    if(!isdigit(ch)) continue;
    else num[j++] = ch;
  }
  dt.tm_sec = atoi(num);
  //Serial.printf("Y:%d M:%d D:%d H:%d M:%d S:%d\n", dt.tm_year, dt.tm_mon, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
  return Timestamp::mkUTCTime(&dt);
}
time_t Timestamp::getUTC(time_t t) {
  tm tmUTC;
  gmtime_r(&t, &tmUTC);
  return mktime(&tmUTC);
}
char * Timestamp::getISOTime() { return this->getISOTime(this->getUTC()); }
char * Timestamp::getISOTime(time_t epoch) {
  struct tm *dt = localtime((time_t *)&epoch);
  return this->formatISO(dt, this->tzOffset());
}
char * Timestamp::formatISO(struct tm *dt, int tz) {
  int tzHrs = floor(tz/100);
  int tzMin = tz - (tzHrs * 100);
  int ms = millis() % 1000;
  snprintf(this->_timeBuffer, sizeof(this->_timeBuffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03d%s%02d%02d", 
    dt->tm_year + 1900, dt->tm_mon + 1, dt->tm_mday, dt->tm_hour, dt->tm_min, dt->tm_sec, ms, tzHrs < 0 ? "-" : "+", abs(tzHrs), abs(tzMin));
  return this->_timeBuffer;
}
int Timestamp::calcTZOffset(time_t *dt) {
  tm tmLocal, tmUTC;
  gmtime_r(dt, &tmUTC);
  localtime_r(dt, &tmLocal);
  long diff = mktime(&tmLocal) - mktime(&tmUTC);
  if(tmLocal.tm_isdst) diff += 3600;
  int hrs = (int)((diff/3600) * 100);
  int mins = diff - (hrs * 36);
  return hrs + mins;
}
int Timestamp::tzOffset() {
  time_t now;
  time(&now);
  return Timestamp::calcTZOffset(&now);
}
