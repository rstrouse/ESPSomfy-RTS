#include <unity.h>
#include <vector>
#include "ShadeBehaviour.h"

// Scenario tests pinning what ShadeBehaviour does today, moved unchanged from
// SomfyShade.  Where the copies of a rule disagree the test says "today:" so
// a later reconcile shows up as a test change (the one left is #85).

#define ASSERT_POS(expected, actual) TEST_ASSERT_FLOAT_WITHIN(0.5f, (expected), (actual))
#define ASSERT_CMD(expected, actual) TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual))

static const uint32_t ADDRESS = 0x123456;
static const uint32_t REMOTE = 0x654321;

// One command the shade sent through its MotorLink.
struct Sent {
  somfy_commands cmd;  // As it went on air, after flipCommands.
  uint8_t repeat;
  uint8_t stepSize;
  uint16_t delayMs;
  bool priority;
  uint32_t ticket;
};
// Builds the frame the way SomfyRemote::sendCommandAfter does and hands out
// tickets 1, 2, 3, ...  gpio sends without a ticket, refuseNext makes the
// radio refuse the next command.
struct FakeLink : MotorLink {
  somfy_frame_t &lastFrame;
  const RemoteSettings &remote;
  std::vector<Sent> sent;
  uint16_t rollingCode = 100;
  uint32_t nextTicket = 1;
  bool refuseNext = false;
  bool gpio = false;
  FakeLink(somfy_frame_t &lastFrame, const RemoteSettings &remote) : lastFrame(lastFrame), remote(remote) {}
  send_result_t send(somfy_commands cmd, uint8_t repeat, uint8_t stepSize, uint16_t delayMs, bool priority) override {
    send_result_t result;
    if(this->refuseNext) {
      this->refuseNext = false;
      return result;
    }
    somfy_frame_t frame = this->lastFrame;
    frame.rollingCode = this->rollingCode + 1;
    frame.remoteAddress = ADDRESS;
    frame.cmd = this->remote.flipCommands ? flipCommand(cmd) : cmd;
    frame.repeats = repeat;
    frame.bitLength = this->remote.bitLength;
    frame.stepSize = stepSize;
    frame.valid = true;
    frame.encKey = 0xA0 | static_cast<uint8_t>(frame.rollingCode & 0x000F);
    frame.processed = false;
    if(!this->gpio) {
      somfy_tx_job_t job;
      encodeTxJob(job, frame, repeat);
      result.ticket = this->nextTicket++;
    }
    result.sent = true;
    this->rollingCode = frame.rollingCode;
    this->lastFrame = frame;
    this->sent.push_back({frame.cmd, repeat, stepSize, delayMs, priority, result.ticket});
    return result;
  }
};
struct Command {
  somfy_commands cmd;
  std::string source;
};
struct FakeEvents : ShadeEvents {
  uint16_t changed = 0;
  std::vector<Command> commands;
  int saves = 0;
  int sensorFlags = 0;
  void onChanged(uint16_t bits) override { this->changed |= bits; }
  void onCommand(somfy_commands cmd, const char *source, uint32_t) override { this->commands.push_back({cmd, source}); }
  void onSaveWanted() override { this->saves++; }
  void onSensorFlags() override { this->sensorFlags++; }
};

// A shade with its settings, link and events, and a clock.
struct Rig {
  ShadeSettings settings;
  RemoteSettings remote;
  somfy_frame_t lastFrame;
  FakeLink link{lastFrame, remote};
  FakeEvents events;
  uint8_t id = 1;
  char name[21] = "Test";
  ShadeBehaviour b{settings, remote, lastFrame, link, events, id, name};
  uint32_t now = 1000;
  uint16_t remoteCode = 500;
  Rig() {
    this->remote.bitLength = 56;
    this->lastFrame.remoteAddress = ADDRESS;
  }
  const ShadeBehaviour::State &st() const { return this->b.state(); }
  // loop() passes for ms, 10 ms apart.
  void run(uint32_t ms) {
    for(uint32_t t = 0; t < ms; t += 10) {
      this->now += 10;
      this->b.tick(this->now);
    }
  }
  void event(tx_event_t type, uint32_t ticket) {
    somfy_tx_event_t evt;
    evt.type = type;
    evt.ticket = ticket;
    evt.at = this->now;
    this->b.onTxEvent(evt);
  }
  // The radio starts the last job sent.
  void onAir() { this->event(tx_event_t::started, this->lastSent().ticket); }
  const Sent &lastSent() const { return this->link.sent.back(); }
  // A remote press: the first frame, then repeats more of the same frame.
  void press(somfy_commands cmd, uint8_t repeats = 0, uint32_t address = ADDRESS, uint8_t stepSize = 0) {
    somfy_frame_t frame;
    frame.valid = true;
    frame.cmd = cmd;
    frame.stepSize = stepSize;
    frame.remoteAddress = address;
    frame.rollingCode = ++this->remoteCode;
    this->b.onFrame(frame, false, this->now);
    for(uint8_t i = 0; i < repeats; i++) {
      this->now += 30;
      this->b.onFrame(frame, false, this->now);
    }
  }
  // Receive passes with no frame, as loop() calls onWaitingFrame.
  void wait(uint32_t ms) {
    for(uint32_t t = 0; t < ms; t += 10) {
      this->now += 10;
      this->b.onWaitingFrame(this->now);
      this->b.tick(this->now);
    }
  }
  void sensorFrame(bool sunny, bool windy) {
    somfy_frame_t frame;
    frame.valid = true;
    frame.cmd = somfy_commands::Sensor;
    frame.remoteAddress = ADDRESS;
    // The sensor's bits ride in the rolling code, shifted down by four.
    frame.rollingCode = (sunny ? 0x02 : 0) | (windy ? 0x01 : 0);
    this->b.onFrame(frame, false, this->now);
  }
};

void setUp() {}
void tearDown() {}

/*********************************************************************
 * Commands from the web, MQTT and Home Assistant
 ********************************************************************/
static void test_down_waits_for_the_radio_then_travels() {
  Rig r;
  TEST_ASSERT_TRUE(r.b.sendCommand(somfy_commands::Down, 1, 0, r.now));
  ASSERT_CMD(somfy_commands::Down, r.lastSent().cmd);
  ASSERT_POS(100, r.st().target);
  r.run(2000);
  // The motor hasn't started until the command is on air.
  ASSERT_POS(0, r.st().currentPos);
  TEST_ASSERT_EQUAL_INT8(1, r.st().direction);
  r.onAir();
  r.run(5000);
  ASSERT_POS(50, r.st().currentPos);
  r.run(5100);
  ASSERT_POS(100, r.st().currentPos);
  TEST_ASSERT_EQUAL_INT8(0, r.st().direction);
  TEST_ASSERT_TRUE(r.events.saves > 0);
  TEST_ASSERT_TRUE(r.events.changed & SHADE_CHANGED_POSITION);
  // A run to the end limit sends no stop.
  TEST_ASSERT_EQUAL(1, r.link.sent.size());
}
static void test_refused_command_moves_nothing() {
  Rig r;
  r.link.refuseNext = true;
  TEST_ASSERT_FALSE(r.b.sendCommand(somfy_commands::Down, 1, 0, r.now));
  ASSERT_POS(0, r.st().target);
  r.run(1000);
  TEST_ASSERT_EQUAL_INT8(0, r.st().direction);
  TEST_ASSERT_EQUAL(0, r.events.commands.size());
}
static void test_hold_times_out_without_a_radio_event() {
  Rig r;
  r.b.sendCommand(somfy_commands::Down, 1, 0, r.now);
  r.run(29000);
  ASSERT_POS(0, r.st().currentPos);
  r.run(2000);
  // After 30 s it moves anyway, timed from the end of the hold.
  TEST_ASSERT_TRUE(r.st().currentPos > 0);
}
static void test_gpio_command_moves_at_once() {
  Rig r;
  r.link.gpio = true;
  r.b.sendCommand(somfy_commands::Down, 1, 0, r.now);
  r.run(5000);
  ASSERT_POS(50, r.st().currentPos);
}
static void test_same_direction_again_stops_tracking() {
  Rig r;
  r.b.sendCommand(somfy_commands::Down, 1, 0, r.now);
  r.onAir();
  r.run(3000);
  TEST_ASSERT_TRUE(r.b.sendCommand(somfy_commands::Down, 1, 0, r.now));
  // The motor stops by itself when it gets the same direction again.
  ASSERT_POS(r.st().currentPos, r.st().target);
  r.run(1000);
  ASSERT_POS(30, r.st().currentPos);
  TEST_ASSERT_EQUAL_INT8(0, r.st().direction);
}
static void test_stop_from_the_web_works_through_the_echo() {
  Rig r;
  r.b.sendCommand(somfy_commands::Down, 1, 0, r.now);
  r.onAir();
  r.run(4000);
  // sendCommand only sends Stop; hearing its own frame is what stops the shade.
  r.b.sendCommand(somfy_commands::Stop, 1, 0, r.now);
  ASSERT_CMD(somfy_commands::Stop, r.lastSent().cmd);
  ASSERT_POS(40, r.st().target);
  ASSERT_CMD(somfy_commands::Stop, r.events.commands.back().cmd);
  TEST_ASSERT_EQUAL_STRING("internal", r.events.commands.back().source.c_str());
}
static void test_move_to_target_sends_a_stop_there() {
  Rig r;
  TEST_ASSERT_TRUE(r.b.moveToTarget(40, -1, r.now));
  ASSERT_CMD(somfy_commands::Down, r.lastSent().cmd);
  r.onAir();
  r.run(4100);
  ASSERT_POS(40, r.st().currentPos);
  TEST_ASSERT_EQUAL(2, r.link.sent.size());
  ASSERT_CMD(somfy_commands::My, r.lastSent().cmd);
  TEST_ASSERT_TRUE(r.lastSent().priority);
  r.event(tx_event_t::finished, r.lastSent().ticket);
  r.run(1000);
  ASSERT_POS(40, r.st().currentPos);
}
static void test_dropped_stop_runs_on_to_the_limit() {
  Rig r;
  r.b.moveToTarget(40, -1, r.now);
  r.onAir();
  r.run(4100);
  r.event(tx_event_t::dropped, r.lastSent().ticket);
  ASSERT_POS(100, r.st().target);
  r.run(7000);
  ASSERT_POS(100, r.st().currentPos);
}
static void test_refused_stop_runs_on_to_the_limit() {
  Rig r;
  r.b.moveToTarget(40, -1, r.now);
  r.onAir();
  r.run(3900);
  r.link.refuseNext = true;
  r.run(200);
  TEST_ASSERT_EQUAL(1, r.link.sent.size());
  ASSERT_POS(100, r.st().target);
}
static void test_a_step_after_move_to_target_sends_no_stop() {
  // The stop at the target ends the positioning, so a later step stops by
  // itself.  A stop sent then would be a My to a still motor: its favorite.
  Rig r;
  r.b.moveToTarget(40, -1, r.now);
  r.onAir();
  r.run(4100);
  r.event(tx_event_t::finished, r.lastSent().ticket);
  const size_t before = r.link.sent.size();
  r.b.sendCommand(somfy_commands::StepDown, 1, 1, r.now);
  r.onAir();
  r.run(2000);
  TEST_ASSERT_EQUAL(before + 1, r.link.sent.size());
  ASSERT_CMD(somfy_commands::StepDown, r.lastSent().cmd);
  TEST_ASSERT_EQUAL_INT8(0, r.st().direction);
}
static void test_a_group_step_after_move_to_target_sends_no_stop() {
  Rig r;
  r.b.moveToTarget(40, -1, r.now);
  r.onAir();
  r.run(4100);
  const size_t before = r.link.sent.size();
  r.b.onGroupCommand(somfy_commands::StepDown, 1, r.now);
  r.run(1000);
  ASSERT_POS(41, r.st().currentPos);
  TEST_ASSERT_EQUAL(before, r.link.sent.size());
}
static void test_toggle_on_a_56_bit_shade_sends_my() {
  Rig r;
  r.b.sendCommand(somfy_commands::Toggle, 1, 0, r.now);
  ASSERT_CMD(somfy_commands::My, r.lastSent().cmd);
  r.remote.bitLength = 80;
  r.b.sendCommand(somfy_commands::Toggle, 1, 0, r.now);
  ASSERT_CMD(somfy_commands::Toggle, r.lastSent().cmd);
}
static void test_flipped_shade_sends_the_other_way() {
  Rig r;
  r.remote.flipCommands = true;
  r.b.sendCommand(somfy_commands::Up, 1, 0, r.now);
  // On air it is Down; the shade itself still heads for the top.
  ASSERT_CMD(somfy_commands::Down, r.lastSent().cmd);
  ASSERT_POS(0, r.st().target);
}

/*********************************************************************
 * Frames from remotes
 ********************************************************************/
static void test_remote_down_and_my_stop() {
  Rig r;
  r.press(somfy_commands::Down);
  ASSERT_POS(100, r.st().target);
  ASSERT_CMD(somfy_commands::Down, r.events.commands.back().cmd);
  TEST_ASSERT_EQUAL_STRING("remote", r.events.commands.back().source.c_str());
  // A remote's frame is already on air, so nothing is held.
  r.run(3000);
  ASSERT_POS(30, r.st().currentPos);
  r.press(somfy_commands::My);
  ASSERT_POS(30, r.st().target);
  r.run(100);
  TEST_ASSERT_EQUAL_INT8(0, r.st().direction);
  TEST_ASSERT_EQUAL(0, r.link.sent.size());
}
static void test_remote_my_when_idle_goes_to_the_favorite_after_half_a_second() {
  Rig r;
  r.b.restore(0, 0, 60, -1);
  r.press(somfy_commands::My);
  ASSERT_POS(0, r.st().target);
  r.wait(400);
  ASSERT_POS(0, r.st().target);
  r.wait(200);
  ASSERT_POS(60, r.st().target);
}
static void test_remote_my_waits_half_a_second_across_the_millis_wrap() {
  Rig r;
  r.b.restore(0, 0, 60, -1);
  r.now = 0xFFFFFFFF - 200;
  r.press(somfy_commands::My);
  // The deadline is past the wrap; it still waits for it.
  r.wait(300);
  ASSERT_POS(0, r.st().target);
  r.wait(300);
  ASSERT_POS(60, r.st().target);
}
static void test_remote_my_held_sets_the_favorite() {
  Rig r;
  r.b.restore(30, 0, 60, -1);
  r.press(somfy_commands::My, 35);
  r.wait(600);
  ASSERT_POS(30, r.st().myPos);
  ASSERT_POS(30, r.st().target);
  TEST_ASSERT_TRUE(r.events.saves > 0);
}
static void test_remote_held_up_on_a_tilt_motor_tilts() {
  Rig r;
  r.settings.shadeType = shade_types::blind;
  r.settings.tiltType = tilt_types::tiltmotor;
  r.b.restore(50, 50, -1, -1);
  r.press(somfy_commands::Up);
  r.wait(600);
  // A short press moves the lift.
  ASSERT_POS(0, r.st().target);
  ASSERT_POS(50, r.st().tiltTarget);

  Rig h;
  h.settings.shadeType = shade_types::blind;
  h.settings.tiltType = tilt_types::tiltmotor;
  h.b.restore(50, 50, -1, -1);
  h.press(somfy_commands::Up, 15);
  h.wait(600);
  // A long one moves the tilt.
  ASSERT_POS(50, h.st().target);
  ASSERT_POS(0, h.st().tiltTarget);
}
static void test_remote_step_uses_the_frames_step_size() {
  Rig r;
  r.b.restore(50, 0, -1, -1);
  // A frame without a step size is one step.
  r.press(somfy_commands::StepDown);
  ASSERT_POS(51, r.st().target);
  // What this shade last sent doesn't matter.
  r.lastFrame.stepSize = 7;
  r.press(somfy_commands::StepDown, 0, ADDRESS, 3);
  // Steps count from where the shade is, still 50.
  ASSERT_POS(53, r.st().target);
}
static void test_a_held_step_button_steps_once() {
  // The repeats of a held step button are the same frame, so it steps once,
  // by the frame's step size, and nothing more once the press is over.
  Rig r;
  r.b.restore(50, 0, -1, -1);
  r.press(somfy_commands::StepDown, 20, ADDRESS, 2);
  ASSERT_POS(52, r.st().target);
  r.wait(1000);
  ASSERT_POS(52, r.st().target);
  ASSERT_POS(52, r.st().currentPos);
  TEST_ASSERT_EQUAL(1, r.events.commands.size());
}
static void test_web_step_uses_the_step_size_sent() {
  Rig r;
  r.b.restore(50, 0, -1, -1);
  r.b.sendCommand(somfy_commands::StepDown, 1, 3, r.now);
  TEST_ASSERT_EQUAL_UINT8(3, r.lastSent().stepSize);
  ASSERT_POS(53, r.st().target);
}
static void test_wind_blocks_a_remote_down() {
  Rig r;
  r.settings.sunSensor = true;
  r.sensorFrame(false, true);
  TEST_ASSERT_EQUAL(1, r.events.sensorFlags);
  TEST_ASSERT_TRUE(r.events.changed & SHADE_CHANGED_WINDY);
  r.press(somfy_commands::Down);
  ASSERT_POS(0, r.st().target);
}
static void test_wind_retracts_the_shade() {
  Rig r;
  r.b.restore(80, 0, -1, -1);
  r.sensorFrame(false, true);
  r.run(2100);
  ASSERT_POS(0, r.st().target);
}
static void test_sun_flag_frame_turns_it_on_and_saves() {
  Rig r;
  r.press(somfy_commands::SunFlag);
  TEST_ASSERT_TRUE(r.st().stateFlags & static_cast<uint8_t>(somfy_flags_t::SunFlag));
  TEST_ASSERT_EQUAL(1, r.events.saves);
  TEST_ASSERT_EQUAL(1, r.events.sensorFlags);
  r.press(somfy_commands::Flag);
  TEST_ASSERT_FALSE(r.st().stateFlags & static_cast<uint8_t>(somfy_flags_t::SunFlag));
}

/*********************************************************************
 * Integrated tilt
 ********************************************************************/
static void integrated(Rig &r) {
  r.settings.shadeType = shade_types::blind;
  r.settings.tiltType = tilt_types::integrated;
  r.settings.tiltTime = 2000;
}
static void test_integrated_tilt_turns_the_slats_first() {
  Rig r;
  integrated(r);
  TEST_ASSERT_TRUE(r.b.moveToTarget(40, 100, r.now));
  r.onAir();
  r.run(1500);
  // Still turning the slats; the lift waits.
  ASSERT_POS(75, r.st().currentTiltPos);
  ASSERT_POS(0, r.st().currentPos);
  r.run(2500);
  ASSERT_POS(100, r.st().currentTiltPos);
  ASSERT_POS(20, r.st().currentPos);
  r.run(2100);
  ASSERT_POS(40, r.st().currentPos);
  ASSERT_CMD(somfy_commands::My, r.lastSent().cmd);
  // The radio drops the stop and the shade runs on to 100%.
  r.event(tx_event_t::dropped, r.lastSent().ticket);
  r.run(6100);
  ASSERT_POS(100, r.st().currentPos);
}
static void test_today_integrated_tilt_turns_back_after_a_lift_target() {
  // today: moveToTarget without a tilt leaves the tilt target where it was,
  // so after the stop the shade turns its slats back with an Up.
  Rig r;
  integrated(r);
  r.b.moveToTarget(40, -1, r.now);
  r.onAir();
  r.run(6100);
  TEST_ASSERT_EQUAL(3, r.link.sent.size());
  ASSERT_CMD(somfy_commands::My, r.link.sent[1].cmd);
  ASSERT_CMD(somfy_commands::Up, r.link.sent[2].cmd);
  ASSERT_POS(0, r.st().tiltTarget);
  r.onAir();
  r.run(2100);
  ASSERT_POS(0, r.st().currentTiltPos);
  ASSERT_POS(40, r.st().currentPos);
}

/*********************************************************************
 * Tilt-only shades: the lift stays all the way down
 ********************************************************************/
enum class Source { web, remote, group };
static const char *sourceName(Source src) {
  switch(src) {
    case Source::web: return "web";
    case Source::remote: return "remote";
    default: return "group";
  }
}
// A tilt-only shade whose favorite tilt is 30, and whose lift something has
// moved to 60.  moving starts the tilt turning first.
static void tiltOnly(Rig &r, bool moving) {
  r.settings.shadeType = shade_types::blind;
  r.settings.tiltType = tilt_types::tiltonly;
  r.b.restore(100, 50, -1, 30);
  if(moving) {
    r.b.onGroupCommand(somfy_commands::Down, 1, r.now);
    r.run(500);
  }
  r.b.setPosition(60, -1);
}
static void tiltOnlyCommand(Rig &r, Source src, somfy_commands cmd) {
  switch(src) {
    case Source::web:
      r.b.sendCommand(cmd, 1, 0, r.now);
      r.onAir();
      break;
    case Source::remote:
      r.press(cmd);
      // Past the wait that tells a short press from a long one.
      r.wait(600);
      break;
    case Source::group:
      r.b.onGroupCommand(cmd, 1, r.now);
      break;
  }
}
static void test_tilt_only_lift_stays_down_whatever_the_source() {
  const Source sources[] = {Source::web, Source::remote, Source::group};
  const somfy_commands cmds[] = {somfy_commands::Up, somfy_commands::Down, somfy_commands::My};
  for(Source src : sources) {
    for(somfy_commands cmd : cmds) {
      for(bool moving : {false, true}) {
        Rig r;
        tiltOnly(r, moving);
        tiltOnlyCommand(r, src, cmd);
        r.run(1000);
        char msg[64];
        snprintf(msg, sizeof(msg), "%s %s%s", sourceName(src), translateSomfyCommand(cmd).c_str(), moving ? " while moving" : "");
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.5f, 100, r.st().target, msg);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.5f, 100, r.st().currentPos, msg);
        TEST_ASSERT_EQUAL_INT8_MESSAGE(0, r.st().direction, msg);
        // What the tilt does.
        if(cmd == somfy_commands::Up) TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.5f, 0, r.st().tiltTarget, msg);
        else if(cmd == somfy_commands::Down) TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.5f, 100, r.st().tiltTarget, msg);
        else if(!moving) TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.5f, 30, r.st().tiltTarget, msg);
      }
    }
  }
}

/*********************************************************************
 * Groups
 ********************************************************************/
static void test_group_down_and_step() {
  Rig r;
  r.b.onGroupCommand(somfy_commands::Down, 1, r.now);
  ASSERT_POS(100, r.st().target);
  // A group command sends nothing itself.
  TEST_ASSERT_EQUAL(0, r.link.sent.size());
  Rig s;
  s.b.restore(50, 0, -1, -1);
  s.b.onGroupCommand(somfy_commands::StepDown, 1, s.now);
  ASSERT_POS(51, s.st().target);
}
static void test_group_sun_flag_needs_a_sensor() {
  Rig r;
  r.b.onGroupCommand(somfy_commands::SunFlag, 1, r.now);
  TEST_ASSERT_FALSE(r.st().stateFlags & static_cast<uint8_t>(somfy_flags_t::SunFlag));
  r.settings.sunSensor = true;
  r.b.onGroupCommand(somfy_commands::SunFlag, 1, r.now);
  TEST_ASSERT_TRUE(r.st().stateFlags & static_cast<uint8_t>(somfy_flags_t::SunFlag));
  TEST_ASSERT_EQUAL(1, r.events.saves);
  // As for a frame, every group works its flags out again.
  TEST_ASSERT_EQUAL(1, r.events.sensorFlags);
  r.b.onGroupCommand(somfy_commands::Flag, 1, r.now);
  TEST_ASSERT_FALSE(r.st().stateFlags & static_cast<uint8_t>(somfy_flags_t::SunFlag));
  TEST_ASSERT_EQUAL(2, r.events.sensorFlags);
}

/*********************************************************************
 * The favorite
 ********************************************************************/
static void test_set_favorite_here_is_committed_when_all_on_air() {
  Rig r;
  r.b.restore(30, 0, -1, -1);
  TEST_ASSERT_TRUE(r.b.setMyPosition(30, -1, r.now));
  ASSERT_CMD(somfy_commands::My, r.lastSent().cmd);
  TEST_ASSERT_EQUAL_UINT8(35, r.lastSent().repeat);
  ASSERT_POS(30, r.st().myPos);
  const int saves = r.events.saves;
  r.event(tx_event_t::finished, r.lastSent().ticket);
  TEST_ASSERT_EQUAL(saves + 1, r.events.saves);
}
static void test_set_favorite_dropped_keeps_the_old_one() {
  Rig r;
  r.b.restore(30, 0, 70, -1);
  r.b.setMyPosition(30, -1, r.now);
  r.event(tx_event_t::dropped, r.lastSent().ticket);
  ASSERT_POS(70, r.st().myPos);
}
static void test_set_favorite_elsewhere_moves_there_first() {
  Rig r;
  TEST_ASSERT_TRUE(r.b.setMyPosition(40, -1, r.now));
  ASSERT_CMD(somfy_commands::Down, r.lastSent().cmd);
  r.onAir();
  r.run(4100);
  // The stop, then the long My 200 ms later that stores it.
  ASSERT_POS(40, r.st().myPos);
  ASSERT_CMD(somfy_commands::My, r.lastSent().cmd);
  TEST_ASSERT_EQUAL_UINT8(35, r.lastSent().repeat);
  TEST_ASSERT_EQUAL_UINT16(200, r.lastSent().delayMs);
}
static void test_sim_my_favorite_moves_to_it() {
  Rig r;
  r.settings.simMy = true;
  r.b.restore(0, 0, 60, -1);
  TEST_ASSERT_TRUE(r.b.moveToMyPosition(r.now));
  ASSERT_CMD(somfy_commands::Down, r.lastSent().cmd);
  ASSERT_POS(60, r.st().target);
}

/*********************************************************************
 * Dry contacts and one-button devices
 ********************************************************************/
static void test_garage_my_toggles_open_and_shut() {
  Rig r;
  r.settings.shadeType = shade_types::garage1;
  r.press(somfy_commands::My);
  ASSERT_POS(100, r.st().target);
  r.run(10100);
  r.press(somfy_commands::My);
  ASSERT_POS(0, r.st().target);
}

/*********************************************************************
 * Time, setup and the outside writers
 ********************************************************************/
static void test_travel_across_the_millis_wrap() {
  Rig r;
  r.now = 0xFFFFFFFF - 2000;
  r.press(somfy_commands::Down);
  r.run(5000);
  ASSERT_POS(50, r.st().currentPos);
}
static void test_restore_set_position_and_clear_tilt() {
  Rig r;
  r.b.restore(42.6f, 10.2f, 30, 5);
  ASSERT_POS(42.6f, r.st().currentPos);
  // The targets are the whole positions.
  TEST_ASSERT_EQUAL_FLOAT(42.0f, r.st().target);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, r.st().tiltTarget);
  TEST_ASSERT_EQUAL_UINT16(0, r.events.changed);
  r.b.setPosition(70, -1);
  ASSERT_POS(70, r.st().currentPos);
  ASSERT_POS(70, r.st().target);
  ASSERT_POS(10.2f, r.st().currentTiltPos);
  TEST_ASSERT_TRUE(r.events.changed & SHADE_CHANGED_POSITION);
  r.b.clearTilt();
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.st().currentTiltPos);
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, r.st().myTiltPos);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_down_waits_for_the_radio_then_travels);
  RUN_TEST(test_refused_command_moves_nothing);
  RUN_TEST(test_hold_times_out_without_a_radio_event);
  RUN_TEST(test_gpio_command_moves_at_once);
  RUN_TEST(test_same_direction_again_stops_tracking);
  RUN_TEST(test_stop_from_the_web_works_through_the_echo);
  RUN_TEST(test_move_to_target_sends_a_stop_there);
  RUN_TEST(test_dropped_stop_runs_on_to_the_limit);
  RUN_TEST(test_refused_stop_runs_on_to_the_limit);
  RUN_TEST(test_a_step_after_move_to_target_sends_no_stop);
  RUN_TEST(test_a_group_step_after_move_to_target_sends_no_stop);
  RUN_TEST(test_toggle_on_a_56_bit_shade_sends_my);
  RUN_TEST(test_flipped_shade_sends_the_other_way);
  RUN_TEST(test_remote_down_and_my_stop);
  RUN_TEST(test_remote_my_when_idle_goes_to_the_favorite_after_half_a_second);
  RUN_TEST(test_remote_my_waits_half_a_second_across_the_millis_wrap);
  RUN_TEST(test_remote_my_held_sets_the_favorite);
  RUN_TEST(test_remote_held_up_on_a_tilt_motor_tilts);
  RUN_TEST(test_remote_step_uses_the_frames_step_size);
  RUN_TEST(test_a_held_step_button_steps_once);
  RUN_TEST(test_web_step_uses_the_step_size_sent);
  RUN_TEST(test_wind_blocks_a_remote_down);
  RUN_TEST(test_wind_retracts_the_shade);
  RUN_TEST(test_sun_flag_frame_turns_it_on_and_saves);
  RUN_TEST(test_integrated_tilt_turns_the_slats_first);
  RUN_TEST(test_today_integrated_tilt_turns_back_after_a_lift_target);
  RUN_TEST(test_tilt_only_lift_stays_down_whatever_the_source);
  RUN_TEST(test_group_down_and_step);
  RUN_TEST(test_group_sun_flag_needs_a_sensor);
  RUN_TEST(test_set_favorite_here_is_committed_when_all_on_air);
  RUN_TEST(test_set_favorite_dropped_keeps_the_old_one);
  RUN_TEST(test_set_favorite_elsewhere_moves_there_first);
  RUN_TEST(test_sim_my_favorite_moves_to_it);
  RUN_TEST(test_garage_my_toggles_open_and_shut);
  RUN_TEST(test_travel_across_the_millis_wrap);
  RUN_TEST(test_restore_set_position_and_clear_tilt);
  return UNITY_END();
}
