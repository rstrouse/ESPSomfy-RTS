#include <Arduino.h>
#include <time.h>
#include "Utils.h"


/*********************************************************************
 * Timestamp class members
 ********************************************************************/
time_t Timestamp::getUTC() { 
  time_t t;
  time(&t);
  return t; 
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
  char isoTime[32];
  snprintf(this->_timeBuffer, sizeof(this->_timeBuffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03d%s%02d%02d", 
    dt->tm_year + 1900, dt->tm_mon + 1, dt->tm_mday, dt->tm_hour, dt->tm_min, dt->tm_sec, ms, tzHrs < 0 ? "-" : "+", abs(tzHrs), abs(tzMin));
  return this->_timeBuffer;
}
int Timestamp::tzOffset() {
  time_t now;
  time(&now);
  tm tmLocal, tmUTC;
  gmtime_r(&now, &tmUTC);
  localtime_r(&now, &tmLocal);
  long diff = mktime(&tmLocal) - mktime(&tmUTC);
  if(tmLocal.tm_isdst) diff += 3600;
  int hrs = (int)((diff/3600) * 100);
  int mins = diff - (hrs * 36);
  return hrs + mins;
}
