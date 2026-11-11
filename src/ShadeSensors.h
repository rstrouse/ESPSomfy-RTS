#ifndef SHADE_SENSORS_H
#define SHADE_SENSORS_H
#include <stdint.h>
#include "ShadeTypes.h"

// Sun and wind automation for a shade with a sun sensor.  Kept free of the
// shade, radio and network so it can be unit tested on the PC
// (pio test -e native).

#define SECS_TO_MILLIS(x) ((x) * 1000)
#define MINS_TO_MILLIS(x) SECS_TO_MILLIS((x) * 60)

// How long the sensor must report a change before the shade follows it.
#define SOMFY_SUN_TIMEOUT MINS_TO_MILLIS(2)
#define SOMFY_NO_SUN_TIMEOUT MINS_TO_MILLIS(20)
#define SOMFY_WIND_TIMEOUT SECS_TO_MILLIS(2)
#define SOMFY_NO_WIND_TIMEOUT MINS_TO_MILLIS(12)
// A remote Down is ignored this long after the last windy sensor frame.
#define SOMFY_NO_WIND_REMOTE_TIMEOUT SECS_TO_MILLIS(30)

// What the weather tells a shade to do.
enum class SensorMove : uint8_t {
  none,
  extend,   // To the favorite, or all the way down when there is none.
  retract   // All the way up.
};

// The sensor timers of one shade.  Times are millis(); a start of 0 means the
// timer isn't running.
struct ShadeSensors {
  uint32_t sunStart = 0;
  uint32_t noSunStart = 0;
  uint32_t windStart = 0;
  uint32_t noWindStart = 0;
  uint32_t windLast = 0;  // Last sensor frame that reported wind.
  bool sunDone = true;
  bool noSunDone = true;
  bool windDone = true;
  bool noWindDone = true;
  void clear();
  // A sensor frame arrived.  was* is what the shade knew before it, sunny and
  // windy what the frame reports.
  void onSensorFrame(bool wasSunny, bool wasWindy, bool sunny, bool windy, uint32_t now);
  // Fires the timers that are due, each once.  Sun only moves the shade while
  // the sun flag is on; wind always does.
  SensorMove check(bool sunFlag, bool sunny, bool windy, uint32_t now);
  // Where turning the sun flag on sends the shade right away.  Nothing while
  // it's windy or while a timer is still running.
  SensorMove onSunFlag(bool sunny, bool windy) const;
  // Whether a remote Down is ignored because the sensor reported wind lately.
  bool windBlocksDown(uint32_t now) const;
};
// The targets a sensor move sets.  A tilt-only shade only moves its tilt and
// every other shade only its lift, as Up and Down do.
struct SensorTargets {
  bool setLift = false;
  float lift = 0.0f;
  bool setTilt = false;
  float tilt = 0.0f;
};
// myPos and myTiltPos are the favorite, -1 when there is none.
SensorTargets sensorTargets(SensorMove move, tilt_types tilt, float myPos, float myTiltPos);
#endif
