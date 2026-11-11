#ifndef SOMFY_SNAPSHOT_H
#define SOMFY_SNAPSHOT_H
#include "ItemSnapshot.h"
#include "Somfy.h"

// Copies a room, shade or group of the shade model into its snapshot.

void snapshotRoom(SomfyRoom &room, room_snapshot_t &out);
void snapshotShade(SomfyShade &shade, shade_snapshot_t &out);
// Works the group's flags out from its shades without changing the group.
void snapshotGroup(SomfyGroup &group, group_snapshot_t &out);
#endif
