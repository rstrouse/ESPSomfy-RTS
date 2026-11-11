#ifndef PROTO_ASSEMBLY_H
#define PROTO_ASSEMBLY_H
#include <stddef.h>
#include <stdint.h>

// Putting a protocol 2 message back together from the pieces the web server
// hands over: a frame larger than a TCP segment, or a message sent as several
// frames (docs/PROTOCOL.md).  One message at a time across every client, in
// one buffer taken when its first piece arrives and given up when it is whole,
// refused or stalled, so nothing is held while no long message is on its way.
// Not thread safe: the caller locks it.  Kept free of the web server so it can
// be unit tested on the PC (pio test -e native).

// How long a message may take to arrive in full.
#define PROTO_ASSEMBLY_TIMEOUT_MS 10000

// One piece of a message.
struct proto_piece_t {
  uint32_t clientId = 0;
  bool first = false;  // its first piece: the start of its first frame
  bool last = false;   // its last piece: the end of its final frame
  bool text = true;    // a text message rather than binary
  const uint8_t *data = nullptr;
  size_t len = 0;
};
enum class assembly_t : uint8_t {
  more,       // wait for the next piece
  complete,   // the message is whole: take() it
  ignored,    // a piece of a message that was already answered
  busy,       // another client's message holds the buffer
  tooLarge,   // over the limit: the rest of it is ignored
  notText,    // a binary message: the rest of it is ignored
  noMemory    // the buffer couldn't be had: the rest of it is ignored
};
class MessageAssembler {
  public:
    explicit MessageAssembler(size_t limit) : limit(limit) {}
    ~MessageAssembler() { this->release(); }
    // Each refusal (busy, tooLarge, notText, noMemory) comes once per message,
    // from the piece that caused it, so the client gets one reply.
    assembly_t add(const proto_piece_t &piece, uint32_t now);
    // The complete message, terminated, for the caller to free(): the
    // assembler holds nothing after it.
    char *take(size_t &len);
    // The client has gone: its part of a message goes with it.
    void drop(uint32_t clientId);
    // Gives up a message that has taken too long, returning its client, which
    // is owed a reply; 0 when none has.  The rest of it is ignored.
    uint32_t expire(uint32_t now);
    bool holding() const { return this->owner != 0; }
  private:
    size_t limit;
    uint32_t owner = 0;
    uint32_t startedAt = 0;
    char *buf = nullptr;
    size_t len = 0;
    void release();
};
#endif
