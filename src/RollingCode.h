#ifndef ROLLING_CODE_H
#define ROLLING_CODE_H
#include <stdint.h>

// Working out a remote's next rolling code (src/SomfyRemote.cpp) and the one
// it goes on with after a load (src/ConfigFile.cpp).  Kept free of NVS
// so it can be unit tested on the PC (pio test -e native).

// The code the next frame uses.  It is only read here and consumed once the
// frame using it has been queued.  synced says last matches what is stored for
// this address, so the store is only read through loadStored until a code has
// been written.
template<typename LoadStored>
inline uint16_t nextRollingCode(bool synced, uint16_t last, LoadStored loadStored) {
  return (uint16_t)((synced ? last : loadStored()) + 1);
}
// The rolling code a remote goes on with after shades.cfg or a backup is
// read.  NVS holds the last code sent and shades.cfg the code when it was
// saved, so a load keeps the higher.  A restore takes the backup's code even
// when it is lower.  A motor ignores codes below the last one it heard, so
// after restoring an older backup to the same device it ignores the remote
// until the codes pass that one.
inline uint16_t rollingCodeAfterLoad(uint16_t stored, uint16_t inFile, bool restoring) {
  if(restoring) return inFile;
  return stored > inFile ? stored : inFile;
}
#endif
