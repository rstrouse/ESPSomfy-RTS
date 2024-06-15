#ifndef utils_h
#define utils_h
#include <Arduino.h>


#define DEBUG_SOMFY Serial




[[maybe_unused]] static void SETCHARPROP(char *prop, const char *value, size_t size) {strncpy(prop, value, size); prop[size - 1] = '\0';}
/*
namespace util { 
  // Createa a custom to_string function.  C++ can be annoying
  // with all the trailing 0s on number formats.
  template <typename T> std::string to_string(const T& t) {
    std::string str{std::to_string (t)};
    int offset{1};
    if (str.find_last_not_of('0') == str.find('.')) {
      offset = 0;     
    }
    str.erase ( str.find_last_not_of('0') + 1, std::string::npos ); 
    str.erase ( str.find_last_not_of('.') + 1, std::string::npos );    
    return str; 
  } 
}
*/

static void _ltrim(char *str) {
  int s = 0, j, k = 0;
  int e = strlen(str);
  while(s < e && (str[s] == ' ' || str[s] == '\n' || str[s] == '\r' || str[s] == '\t' || str[s] == '"')) s++;
  if(s > 0) {
    for(j = s; j < e; j++) {
      str[k] = str[j];
      k++;
    }
    str[k] = '\0';
  }
  //if(s > 0) strcpy(str, &str[s]);
}
static void _rtrim(char *str) {
  int e = strlen(str) - 1;
  while(e >= 0 && (str[e] == ' ' || str[e] == '\n' || str[e] == '\r' || str[e] == '\t' || str[e] == '"')) {str[e] = '\0'; e--;}
}
[[maybe_unused]] static void _trim(char *str) { _ltrim(str); _rtrim(str); }
struct rebootDelay_t {
  bool reboot = false;
  int rebootTime = 0;
  bool closed = false;
};
[[maybe_unused]] static bool toBoolean(const char *str, bool def) {
  if(!str) return def;
  if(strlen(str) == 0) return def;
  else if(str[0] == 't' || str[0] == 'T' || str[0] == '1') return true;
  else if(str[0] == 'f' || str[0] == 'F' || str[0] == '0') return false;
  return def;
}

class Timestamp {
  char _timeBuffer[128];
  public:
    time_t getUTC();
    time_t getUTC(time_t epoch);
    char * getISOTime();
    char * getISOTime(time_t epoch);
    char * formatISO(struct tm *dt, int tz);
    int tzOffset();
    static time_t parseUTCTime(const char *buff);
    static time_t mkUTCTime(struct tm *dt);
    static int calcTZOffset(time_t *dt);
    static time_t now();
    static unsigned long epoch();
};
// Sort an array
template<typename AnyType> void sortArray(AnyType array[], size_t sizeOfArray);
// Sort in reverse
template<typename AnyType> void sortArrayReverse(AnyType array[], size_t sizeOfArray);
// Sort an array with custom comparison function
template<typename AnyType> void sortArray(AnyType array[], size_t sizeOfArray, bool (*largerThan)(AnyType, AnyType));
// Sort in reverse with custom comparison function
template<typename AnyType> void sortArrayReverse(AnyType array[], size_t sizeOfArray, bool (*largerThan)(AnyType, AnyType));
namespace ArduinoSort {
  template<typename AnyType> bool builtinLargerThan(AnyType first, AnyType second) { return first > second; }
  //template<> bool builtinLargerThan(char* first, char* second) { return strcmp(first, second) > 0; }
  template<typename AnyType> void insertionSort(AnyType array[], size_t sizeOfArray, bool reverse, bool (*largerThan)(AnyType, AnyType)) { for (size_t i = 1; i < sizeOfArray; i++) {
    for (size_t j = i; j > 0 && (largerThan(array[j-1], array[j]) != reverse); j--) {
        AnyType tmp = array[j-1];
        array[j-1] = array[j];
        array[j] = tmp;
      }
    }
  }
}
template<typename AnyType> void sortArray(AnyType array[], size_t sizeOfArray) { ArduinoSort::insertionSort(array, sizeOfArray, false, ArduinoSort::builtinLargerThan); }
template<typename AnyType> void sortArrayReverse(AnyType array[], size_t sizeOfArray) { ArduinoSort::insertionSort(array, sizeOfArray, true, ArduinoSort::builtinLargerThan); }
template<typename AnyType> void sortArray(AnyType array[], size_t sizeOfArray, bool (*largerThan)(AnyType, AnyType)) { ArduinoSort::insertionSort(array, sizeOfArray, false, largerThan); }
template<typename AnyType> void sortArrayReverse(AnyType array[], size_t sizeOfArray, bool (*largerThan)(AnyType, AnyType)) { ArduinoSort::insertionSort(array, sizeOfArray, true, largerThan); }
#endif
