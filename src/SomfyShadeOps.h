#ifndef SOMFY_SHADE_OPS_H
#define SOMFY_SHADE_OPS_H
#include "ShadeRequests.h"

// ShadeOps on the device: forwards to the shades and groups of the shade
// model.  Runs on loop() only.
class SomfyShadeOps : public ShadeOps {
  public:
    bool findShade(uint8_t shadeId, shade_info_t &info) override;
    bool findGroup(uint8_t groupId, uint8_t &repeats) override;
    bool moveToTarget(uint8_t shadeId, float pos) override;
    bool moveToTiltTarget(uint8_t shadeId, float tilt) override;
    bool sendCommand(uint8_t shadeId, somfy_commands cmd, uint8_t repeat, uint8_t stepSize) override;
    bool sendTiltCommand(uint8_t shadeId, somfy_commands cmd) override;
    bool isLastCommand(uint8_t shadeId, somfy_commands cmd) override;
    bool repeatFrame(uint8_t shadeId, uint8_t repeat) override;
    bool sendSensorCommand(uint8_t shadeId, int8_t windy, int8_t sunny, uint8_t repeat) override;
    bool setMyPosition(uint8_t shadeId, int8_t pos, int8_t tilt) override;
    void setPositions(uint8_t shadeId, float pos, float tilt) override;
    void setUnpaired(uint8_t shadeId) override;
    void setRollingCode(uint8_t shadeId, uint16_t code) override;
    bool sendGroupCommand(uint8_t groupId, somfy_commands cmd, uint8_t repeat, uint8_t stepSize) override;
    bool isLastGroupCommand(uint8_t groupId, somfy_commands cmd) override;
    bool repeatGroupFrame(uint8_t groupId, uint8_t repeat) override;
    bool sendGroupSensorCommand(uint8_t groupId, int8_t windy, int8_t sunny, uint8_t repeat) override;
};
#endif
