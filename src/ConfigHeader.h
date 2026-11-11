#ifndef CONFIG_HEADER_H
#define CONFIG_HEADER_H
#include <stdint.h>

// The header of a shades.cfg file: its version and how many records of each
// kind follow, and their sizes.  Kept free of the file system so it can be
// unit tested on the PC (pio test -e native).
struct config_header_t {
  uint8_t version = 1;
  uint8_t repeaterRecords = 0;
  uint8_t repeaterRecordSize = 0;
  uint8_t roomRecords = 0;
  uint16_t roomRecordSize = 0;
  uint16_t shadeRecordSize = 0;
  uint8_t shadeRecords = 0;
  uint16_t groupRecordSize = 0;
  uint8_t groupRecords = 0;
  uint16_t settingsRecordSize = 0;
  uint16_t netRecordSize = 0;
  uint16_t transRecordSize = 0;
  char serverId[10] = ""; // This must match the server id size in the ConfigSettings.
  int8_t length = 0;
};

// The bytes of the shade model's records, which follow the header in this
// order: rooms, shades, groups.  A restore that leaves the shades out skips
// this many bytes.
inline uint32_t shadeModelBytes(const config_header_t &header) {
  return static_cast<uint32_t>(header.roomRecords) * header.roomRecordSize
    + static_cast<uint32_t>(header.shadeRecords) * header.shadeRecordSize
    + static_cast<uint32_t>(header.groupRecords) * header.groupRecordSize;
}
// The file size the header adds up to, counting only the records its version
// has.  Backups written before the transceiver record's size was corrected
// declare it 6 bytes short, so a mismatch alone doesn't make a file invalid.
inline uint32_t expectedFileBytes(const config_header_t &header) {
  uint32_t bytes = static_cast<uint32_t>(header.length)
    + static_cast<uint32_t>(header.shadeRecords) * header.shadeRecordSize;
  if(header.version > 10) bytes += static_cast<uint32_t>(header.groupRecords) * header.groupRecordSize;
  if(header.version >= 19) bytes += static_cast<uint32_t>(header.roomRecords) * header.roomRecordSize;
  if(header.version > 13) bytes += header.settingsRecordSize + header.netRecordSize + header.transRecordSize;
  if(header.version >= 21) bytes += static_cast<uint32_t>(header.repeaterRecords) * header.repeaterRecordSize;
  return bytes;
}
#endif
