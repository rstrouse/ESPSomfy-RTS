#ifndef PROTO_SOCKET_H
#define PROTO_SOCKET_H
#include <ESPAsyncWebServer.h>
#include "ProtoSchema.h"
#include "ModelEditor.h"
#include "ShadeRequests.h"
#include "ProtoFrame.h"

class SomfyShade;
class SomfyGroup;
class SomfyRoom;

// The socket protocol 2 endpoint, ws://host/ws on port 80 (docs/PROTOCOL.md,
// docs/adr/0005-one-websocket-protocol.md).
//
// async_tcp only reads what clients send and answers with fixed frames that
// hold nothing of the shade model (hello, errors, ok).  Everything formatted
// from the model goes out from loop(), in one frame buffer.  The
// clients' slots and syncs are in ProtoSlots.
class ProtoSocket {
  public:
    // Adds /ws to the server.  From setup(), on loop()'s task.
    void begin(AsyncWebServer &server);
    // Moves each client's sync on.  loop() only.
    void loop();
    // Closes every client, before a reboot: a restart drops the connections
    // without a word, so clients wouldn't know they had gone.
    void end();
    bool hasClients() const;
    // The reply to a command op once loop() has run its shade request.
    void replyRequest(const proto_reply_to_t &to, const shade_request_t &req, shade_request_result_t result);
    // The reply to an edit op once loop() has applied it.
    void replyEdit(const proto_reply_to_t &to, model_edit_type_t type, const edit_result_t &result);
    // The reply to a read op, from the shade model.  loop() only.
    void replyRead(const proto_reply_to_t &to, uint8_t op, uint8_t groupId);
    // Any other reply formatted on loop(): ok, and the fields written between
    // the two.  One too large for a frame goes as an internal error.
    ProtoWriter beginReply(const proto_reply_to_t &to);
    void endReply(const proto_reply_to_t &to);
    void replyRefused(const proto_reply_to_t &to, proto_error_t code, const char *desc);
    // settingsChanged, to every client but the one that saved them.
    void settingsChanged(uint32_t savedBy, const char *section);
    // Closes every client, as a security save does: their tokens may no
    // longer be good, or they had none.  savedBy is closed once its reply has
    // gone, so it reads its new token first.
    void closeAll(uint32_t savedBy);
    // wifiNetworks goes to the clients that asked for a scan since the last
    // one, once it is done: count is WiFi.scanComplete(), and the networks
    // are WiFi's scan results.
    void wantsWifiNetworks(uint32_t clientId);
    void wifiNetworks(int16_t count);
    // Events, loop() only.
    void shadeState(SomfyShade &shade);
    void shade(SomfyShade &shade);
    void shadeRemoved(uint8_t shadeId);
    void group(SomfyGroup &group);
    void groupState(SomfyGroup &group);
    void groupRemoved(uint8_t groupId);
    void room(SomfyRoom &room);
    void roomRemoved(uint8_t roomId);
    // roomOrder, shadeOrder or groupOrder: the list's ids, in sort order.
    void order(sort_list_t list);
    void repeaters();
    void shadeCommanded(SomfyShade &shade, const char *command, const char *source, uint32_t sourceAddress);
    void memory();
    void wifi();
    void ethernet();
    // A feed event: write its fields into the writer between the two.  Only
    // when subscribed() says someone is, which is also only on loop().
    bool subscribed(proto_feed_t feed) const;
    ProtoWriter beginFeed(proto_type_t type);
    void endFeed(proto_feed_t feed);
    static void wsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
  private:
    // On loop()'s task, with a client to send to: nothing is formatted for
    // nobody, or off the task that owns the frame buffer.
    bool sending() const;
};
extern ProtoSocket protoSocket;
#endif
