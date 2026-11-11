#include <unity.h>
#include "RadioScheduler.h"

static const uint32_t REMOTE_A = 0x111111;
static const uint32_t REMOTE_B = 0x222222;

void setUp() {}
void tearDown() {}

static somfy_tx_job_t makeJob(uint32_t remoteAddress, somfy_commands cmd = somfy_commands::Up) {
  somfy_tx_job_t job;
  job.frame.remoteAddress = remoteAddress;
  job.frame.cmd = cmd;
  job.repeats = 2;
  return job;
}
static uint32_t submit(RadioScheduler &s, uint32_t remoteAddress, uint32_t now = 0, bool priority = false, uint16_t delayMs = 0) {
  somfy_tx_job_t job = makeJob(remoteAddress, priority ? somfy_commands::My : somfy_commands::Up);
  return s.submit(job, priority, delayMs, now);
}
// Takes the next job and sends all of it, as the radio task does.  Returns its
// ticket, 0 when none is due.
static uint32_t runNext(RadioScheduler &s, uint32_t now = 0) {
  somfy_tx_job_t job;
  uint32_t waitMs;
  if(!s.next(now, job, waitMs)) return 0;
  if(!s.start(job.ticket, now)) return job.ticket;
  s.ended(job.ticket, true, now);
  return job.ticket;
}
static void assertEvent(RadioScheduler &s, tx_event_t type, uint32_t ticket) {
  somfy_tx_event_t evt;
  TEST_ASSERT_TRUE_MESSAGE(s.popEvent(evt), "expected another event");
  TEST_ASSERT_EQUAL_UINT8((uint8_t)type, (uint8_t)evt.type);
  TEST_ASSERT_EQUAL_UINT32(ticket, evt.ticket);
}
static void assertNoEvents(RadioScheduler &s) {
  somfy_tx_event_t evt;
  TEST_ASSERT_FALSE(s.popEvent(evt));
}
static void discardEvents(RadioScheduler &s) {
  somfy_tx_event_t evt;
  while(s.popEvent(evt)) {}
}

/*********************************************************************
 * Order
 ********************************************************************/
static void test_jobs_go_out_in_the_order_they_came() {
  RadioScheduler s;
  const uint32_t a = submit(s, REMOTE_A);
  const uint32_t b = submit(s, REMOTE_B);
  const uint32_t c = submit(s, REMOTE_A);
  TEST_ASSERT_NOT_EQUAL(0, a);
  TEST_ASSERT_EQUAL_UINT32(a, runNext(s));
  TEST_ASSERT_EQUAL_UINT32(b, runNext(s));
  TEST_ASSERT_EQUAL_UINT32(c, runNext(s));
  TEST_ASSERT_EQUAL_UINT32(0, runNext(s));
}
static void test_tickets_are_distinct_and_set_in_the_job() {
  RadioScheduler s;
  somfy_tx_job_t job = makeJob(REMOTE_A);
  const uint32_t a = s.submit(job, false, 0, 0);
  TEST_ASSERT_EQUAL_UINT32(a, job.ticket);
  TEST_ASSERT_NOT_EQUAL(a, submit(s, REMOTE_A));
}
static void test_nothing_waiting_sleeps_until_woken() {
  RadioScheduler s;
  somfy_tx_job_t job;
  uint32_t waitMs = 0;
  TEST_ASSERT_FALSE(s.next(0, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(RADIO_WAIT_FOREVER, waitMs);
}

/*********************************************************************
 * Stops (priority jobs)
 ********************************************************************/
static void test_stop_goes_ahead_of_other_remotes() {
  RadioScheduler s;
  const uint32_t b1 = submit(s, REMOTE_B);
  const uint32_t b2 = submit(s, REMOTE_B);
  const uint32_t stop = submit(s, REMOTE_A, 0, true);
  TEST_ASSERT_EQUAL_UINT32(stop, runNext(s));
  TEST_ASSERT_EQUAL_UINT32(b1, runNext(s));
  TEST_ASSERT_EQUAL_UINT32(b2, runNext(s));
}
static void test_stop_queues_behind_its_own_remote() {
  // Overtaking the remote's waiting frame would send the rolling codes out of
  // order and the motor would reject the older one.
  RadioScheduler s;
  const uint32_t up = submit(s, REMOTE_A);
  const uint32_t b = submit(s, REMOTE_B);
  const uint32_t stop = submit(s, REMOTE_A, 0, true);
  TEST_ASSERT_EQUAL_UINT32(up, runNext(s));
  TEST_ASSERT_EQUAL_UINT32(b, runNext(s));
  TEST_ASSERT_EQUAL_UINT32(stop, runNext(s));
}
static void test_stop_goes_ahead_once_its_remote_is_on_air() {
  RadioScheduler s;
  const uint32_t up = submit(s, REMOTE_A);
  const uint32_t b = submit(s, REMOTE_B);
  somfy_tx_job_t job;
  uint32_t waitMs;
  TEST_ASSERT_TRUE(s.next(0, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(up, job.ticket);
  // The remote's frame is already going out so the stop can't overtake it.
  const uint32_t stop = submit(s, REMOTE_A, 0, true);
  s.start(up, 0);
  s.ended(up, true, 0);
  TEST_ASSERT_EQUAL_UINT32(stop, runNext(s));
  TEST_ASSERT_EQUAL_UINT32(b, runNext(s));
}
static void test_stops_go_out_in_the_order_they_came() {
  RadioScheduler s;
  const uint32_t back = submit(s, REMOTE_B);
  const uint32_t stopA = submit(s, REMOTE_A, 0, true);
  const uint32_t stopC = submit(s, 0x333333, 0, true);
  TEST_ASSERT_EQUAL_UINT32(stopA, runNext(s));
  TEST_ASSERT_EQUAL_UINT32(stopC, runNext(s));
  TEST_ASSERT_EQUAL_UINT32(back, runNext(s));
}
static void test_second_stop_of_a_remote_stays_behind_the_first() {
  RadioScheduler s;
  const uint32_t back = submit(s, REMOTE_B);
  const uint32_t first = submit(s, REMOTE_A, 0, true);
  const uint32_t second = submit(s, REMOTE_A, 0, true);
  TEST_ASSERT_EQUAL_UINT32(first, runNext(s));
  TEST_ASSERT_EQUAL_UINT32(back, runNext(s));
  TEST_ASSERT_EQUAL_UINT32(second, runNext(s));
}

/*********************************************************************
 * Room
 ********************************************************************/
static void test_normal_jobs_leave_the_reserved_slots() {
  RadioScheduler s;
  for(uint8_t i = 0; i < TX_JOB_QUEUE_SIZE - TX_RESERVED_SLOTS; i++)
    TEST_ASSERT_NOT_EQUAL(0, submit(s, REMOTE_B));
  TEST_ASSERT_FALSE(s.hasRoom());
  TEST_ASSERT_EQUAL_UINT32(0, submit(s, REMOTE_B));
  TEST_ASSERT_NOT_EQUAL(0, submit(s, REMOTE_A, 0, true));
}
static void test_stops_use_the_reserved_slots() {
  RadioScheduler s;
  for(uint8_t i = 0; i < TX_JOB_QUEUE_SIZE - TX_RESERVED_SLOTS; i++) submit(s, REMOTE_B);
  for(uint8_t i = 0; i < TX_RESERVED_SLOTS; i++)
    TEST_ASSERT_NOT_EQUAL(0, submit(s, REMOTE_A, 0, true));
  TEST_ASSERT_EQUAL_UINT32(0, submit(s, REMOTE_A, 0, true));
}
static void test_a_refused_job_has_no_ticket() {
  RadioScheduler s;
  for(uint8_t i = 0; i < TX_JOB_QUEUE_SIZE - TX_RESERVED_SLOTS; i++) submit(s, REMOTE_B);
  somfy_tx_job_t job = makeJob(REMOTE_A);
  job.ticket = 99;
  TEST_ASSERT_EQUAL_UINT32(0, s.submit(job, false, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(0, job.ticket);
}
static void test_taking_a_job_frees_its_slot() {
  RadioScheduler s;
  for(uint8_t i = 0; i < TX_JOB_QUEUE_SIZE - TX_RESERVED_SLOTS; i++) submit(s, REMOTE_B);
  runNext(s);
  TEST_ASSERT_TRUE(s.hasRoom());
}

/*********************************************************************
 * Delayed jobs
 ********************************************************************/
static void test_delayed_job_waits_until_due() {
  RadioScheduler s;
  const uint32_t a = submit(s, REMOTE_A, 1000, false, 500);
  somfy_tx_job_t job;
  uint32_t waitMs;
  TEST_ASSERT_FALSE(s.next(1000, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(500, waitMs);
  TEST_ASSERT_FALSE(s.next(1400, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(100, waitMs);
  TEST_ASSERT_TRUE(s.next(1500, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(a, job.ticket);
}
static void test_delayed_job_holds_up_the_jobs_behind_it() {
  RadioScheduler s;
  const uint32_t a = submit(s, REMOTE_A, 0, false, 500);
  const uint32_t b = submit(s, REMOTE_B);
  TEST_ASSERT_EQUAL_UINT32(0, runNext(s, 100));
  TEST_ASSERT_EQUAL_UINT32(a, runNext(s, 500));
  TEST_ASSERT_EQUAL_UINT32(b, runNext(s, 500));
}
static void test_stop_overtakes_a_delayed_job_of_another_remote() {
  RadioScheduler s;
  const uint32_t a = submit(s, REMOTE_A, 0, false, 500);
  const uint32_t stop = submit(s, REMOTE_B, 100, true);
  TEST_ASSERT_EQUAL_UINT32(stop, runNext(s, 100));
  // The stop went off air at 100 so the delay starts again from there.
  TEST_ASSERT_EQUAL_UINT32(0, runNext(s, 500));
  TEST_ASSERT_EQUAL_UINT32(a, runNext(s, 600));
}
static void test_delay_counts_from_the_job_ahead_going_off_air() {
  // The stop at the target is queued just before the favorite and is on air
  // longer than the favorite's delay.
  RadioScheduler s;
  const uint32_t stop = submit(s, REMOTE_A, 0, true);
  const uint32_t setMy = submit(s, REMOTE_A, 0, false, 200);
  somfy_tx_job_t job;
  uint32_t waitMs;
  TEST_ASSERT_TRUE(s.next(0, job, waitMs));
  TEST_ASSERT_TRUE(s.start(stop, 0));
  s.ended(stop, true, 250);
  TEST_ASSERT_FALSE(s.next(350, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(100, waitMs);
  TEST_ASSERT_TRUE(s.next(450, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(setMy, job.ticket);
}
static void test_delay_ignores_a_job_that_ended_before_it_was_queued() {
  RadioScheduler s;
  submit(s, REMOTE_B, 0);
  runNext(s, 50);
  const uint32_t a = submit(s, REMOTE_A, 100, false, 200);
  somfy_tx_job_t job;
  uint32_t waitMs;
  TEST_ASSERT_FALSE(s.next(100, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(200, waitMs);
  TEST_ASSERT_TRUE(s.next(300, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(a, job.ticket);
}
static void test_delay_after_a_job_ended_across_millis_wrap() {
  RadioScheduler s;
  const uint32_t stop = submit(s, REMOTE_A, 0xFFFFFF00u, true);
  const uint32_t setMy = submit(s, REMOTE_A, 0xFFFFFF00u, false, 0x100);
  somfy_tx_job_t job;
  uint32_t waitMs;
  s.next(0xFFFFFF00u, job, waitMs);
  s.start(stop, 0xFFFFFF00u);
  s.ended(stop, true, 0x80);
  TEST_ASSERT_FALSE(s.next(0x100, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(0x80, waitMs);
  TEST_ASSERT_TRUE(s.next(0x180, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(setMy, job.ticket);
}
static void test_stop_waits_behind_its_remotes_delayed_job() {
  // The favorite's long press holds a rolling code, so the stop can't go first.
  RadioScheduler s;
  const uint32_t setMy = submit(s, REMOTE_A, 0, false, 500);
  const uint32_t stop = submit(s, REMOTE_A, 100, true);
  TEST_ASSERT_EQUAL_UINT32(0, runNext(s, 100));
  TEST_ASSERT_EQUAL_UINT32(setMy, runNext(s, 500));
  TEST_ASSERT_EQUAL_UINT32(stop, runNext(s, 500));
}
static void test_delay_across_millis_wrap() {
  RadioScheduler s;
  const uint32_t a = submit(s, REMOTE_A, 0xFFFFFF00u, false, 0x200);
  somfy_tx_job_t job;
  uint32_t waitMs;
  TEST_ASSERT_FALSE(s.next(0xFFFFFF00u, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(0x200, waitMs);
  TEST_ASSERT_FALSE(s.next(0x50, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(0xB0, waitMs);
  TEST_ASSERT_TRUE(s.next(0x100, job, waitMs));
  TEST_ASSERT_EQUAL_UINT32(a, job.ticket);
}

/*********************************************************************
 * Outcomes
 ********************************************************************/
static void test_job_reports_started_then_finished() {
  RadioScheduler s;
  const uint32_t a = submit(s, REMOTE_A);
  somfy_tx_job_t job;
  uint32_t waitMs;
  s.next(10, job, waitMs);
  TEST_ASSERT_TRUE(s.start(a, 20));
  TEST_ASSERT_TRUE(s.keepSending(a));
  s.ended(a, true, 300);
  somfy_tx_event_t evt;
  TEST_ASSERT_TRUE(s.popEvent(evt));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)tx_event_t::started, (uint8_t)evt.type);
  TEST_ASSERT_EQUAL_UINT32(a, evt.ticket);
  TEST_ASSERT_EQUAL_UINT32(20, evt.at);
  TEST_ASSERT_EQUAL_UINT32(REMOTE_A, evt.remoteAddress);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)somfy_commands::Up, (uint8_t)evt.cmd);
  TEST_ASSERT_TRUE(s.popEvent(evt));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)tx_event_t::finished, (uint8_t)evt.type);
  TEST_ASSERT_EQUAL_UINT32(300, evt.at);
  assertNoEvents(s);
}
static void test_cut_short_job_reports_aborted() {
  RadioScheduler s;
  const uint32_t a = submit(s, REMOTE_A);
  somfy_tx_job_t job;
  uint32_t waitMs;
  s.next(0, job, waitMs);
  s.start(a, 0);
  s.ended(a, false, 0);
  assertEvent(s, tx_event_t::started, a);
  assertEvent(s, tx_event_t::aborted, a);
  assertNoEvents(s);
}
static void test_flush_drops_every_waiting_job_once() {
  RadioScheduler s;
  const uint32_t a = submit(s, REMOTE_A);
  const uint32_t stop = submit(s, REMOTE_B, 0, true);
  const uint32_t delayed = submit(s, REMOTE_A, 0, false, 5000);
  s.flush(0);
  assertEvent(s, tx_event_t::dropped, stop);
  assertEvent(s, tx_event_t::dropped, a);
  assertEvent(s, tx_event_t::dropped, delayed);
  assertNoEvents(s);
  TEST_ASSERT_EQUAL_UINT32(0, runNext(s, 5000));
}
static void test_flush_cuts_short_the_job_on_air() {
  RadioScheduler s;
  const uint32_t a = submit(s, REMOTE_A);
  somfy_tx_job_t job;
  uint32_t waitMs;
  s.next(0, job, waitMs);
  s.start(a, 0);
  s.flush(0);
  TEST_ASSERT_FALSE(s.keepSending(a));
  s.ended(a, false, 0);
  assertEvent(s, tx_event_t::started, a);
  assertEvent(s, tx_event_t::aborted, a);
  assertNoEvents(s);
}
static void test_job_flushed_before_it_starts_is_dropped() {
  // The radio task took it off the queue, then a flush came before it got the
  // radio.
  RadioScheduler s;
  const uint32_t a = submit(s, REMOTE_A);
  somfy_tx_job_t job;
  uint32_t waitMs;
  s.next(0, job, waitMs);
  s.flush(0);
  TEST_ASSERT_FALSE(s.start(a, 0));
  assertEvent(s, tx_event_t::dropped, a);
  assertNoEvents(s);
}
static void test_every_job_gets_one_outcome_across_a_flush() {
  RadioScheduler s;
  uint32_t tickets[6];
  for(uint8_t i = 0; i < 3; i++) tickets[i] = submit(s, REMOTE_A + i);
  somfy_tx_job_t job;
  uint32_t waitMs;
  s.next(0, job, waitMs);
  s.start(job.ticket, 0);
  s.flush(0);
  // Jobs queued after the flush are unaffected by it.
  for(uint8_t i = 3; i < 6; i++) tickets[i] = submit(s, REMOTE_B + i);
  s.ended(job.ticket, s.keepSending(job.ticket), 0);
  while(runNext(s)) {}
  uint8_t outcomes[6] = {};
  somfy_tx_event_t evt;
  while(s.popEvent(evt)) {
    if(evt.type == tx_event_t::started) continue;
    for(uint8_t i = 0; i < 6; i++)
      if(tickets[i] == evt.ticket) outcomes[i]++;
  }
  for(uint8_t i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT8(1, outcomes[i]);
}
static void test_no_outcome_is_lost_when_events_arent_collected() {
  // loop() collects the events.  If it stalls, jobs are refused rather than
  // their outcomes thrown away.
  RadioScheduler s;
  uint32_t accepted = 0;
  for(uint32_t i = 0; i < 200; i++) {
    if(!submit(s, REMOTE_A)) break;
    accepted++;
    runNext(s);
  }
  TEST_ASSERT_TRUE(accepted > 0);
  TEST_ASSERT_TRUE(accepted < 200);
  TEST_ASSERT_FALSE(s.hasRoom());
  uint32_t started = 0, finished = 0;
  somfy_tx_event_t evt;
  while(s.popEvent(evt)) {
    if(evt.type == tx_event_t::started) started++;
    else if(evt.type == tx_event_t::finished) finished++;
  }
  TEST_ASSERT_EQUAL_UINT32(accepted, started);
  TEST_ASSERT_EQUAL_UINT32(accepted, finished);
  TEST_ASSERT_TRUE(s.hasRoom());
  TEST_ASSERT_NOT_EQUAL(0, submit(s, REMOTE_A));
}
static void test_flush_reports_every_drop_when_events_arent_collected() {
  RadioScheduler s;
  uint32_t tickets[64];
  uint8_t n = 0;
  // Outcomes of jobs already sent take up part of the event ring.
  for(uint8_t i = 0; i < 20; i++) {
    tickets[n] = submit(s, REMOTE_A);
    TEST_ASSERT_NOT_EQUAL(0, tickets[n]);
    n++;
    runNext(s);
  }
  // Queue as many as are taken.
  for(uint32_t t; n < 64 && (t = submit(s, REMOTE_B, 0, true)) != 0;) tickets[n++] = t;
  TEST_ASSERT_TRUE(n > 20);
  s.flush(0);
  uint8_t outcomes[64] = {};
  somfy_tx_event_t evt;
  while(s.popEvent(evt)) {
    if(evt.type == tx_event_t::started) continue;
    for(uint8_t i = 0; i < n; i++)
      if(tickets[i] == evt.ticket) outcomes[i]++;
  }
  for(uint8_t i = 0; i < n; i++) TEST_ASSERT_EQUAL_UINT8(1, outcomes[i]);
}
static void test_unknown_ticket_is_ignored() {
  RadioScheduler s;
  TEST_ASSERT_FALSE(s.keepSending(0));
  TEST_ASSERT_FALSE(s.keepSending(42));
  TEST_ASSERT_FALSE(s.start(42, 0));
  s.ended(42, true, 0);
  assertNoEvents(s);
}

/*********************************************************************
 * Suspend
 ********************************************************************/
static void test_suspend_flushes_and_refuses_jobs() {
  RadioScheduler s;
  const uint32_t a = submit(s, REMOTE_A);
  s.suspend(TX_SUSPEND_SCAN, 0);
  TEST_ASSERT_TRUE(s.suspended());
  assertEvent(s, tx_event_t::dropped, a);
  TEST_ASSERT_FALSE(s.hasRoom());
  TEST_ASSERT_EQUAL_UINT32(0, submit(s, REMOTE_A));
  TEST_ASSERT_EQUAL_UINT32(0, submit(s, REMOTE_A, 0, true));
  assertNoEvents(s);
}
static void test_resume_needs_every_reason_cleared() {
  RadioScheduler s;
  s.suspend(TX_SUSPEND_SCAN, 0);
  s.suspend(TX_SUSPEND_OTA, 0);
  s.resume(TX_SUSPEND_SCAN);
  TEST_ASSERT_TRUE(s.suspended());
  TEST_ASSERT_EQUAL_UINT32(0, submit(s, REMOTE_A));
  s.resume(TX_SUSPEND_OTA);
  TEST_ASSERT_FALSE(s.suspended());
  TEST_ASSERT_NOT_EQUAL(0, submit(s, REMOTE_A));
}

/*********************************************************************
 * Repeater frames
 ********************************************************************/
static void test_repeater_frames_report_nothing() {
  RadioScheduler s;
  somfy_tx_job_t job = makeJob(REMOTE_A);
  job.repeater = true;
  const uint32_t r = s.submit(job, false, 0, 0);
  TEST_ASSERT_NOT_EQUAL(0, r);
  TEST_ASSERT_EQUAL_UINT32(r, runNext(s));
  submit(s, REMOTE_B);
  job = makeJob(REMOTE_A);
  job.repeater = true;
  s.submit(job, false, 0, 0);
  s.flush(0);
  somfy_tx_event_t evt;
  TEST_ASSERT_TRUE(s.popEvent(evt));
  TEST_ASSERT_EQUAL_UINT32(REMOTE_B, evt.remoteAddress);
  assertNoEvents(s);
}
static void test_repeater_frame_doesnt_hold_back_a_stop() {
  // It isn't one of our remotes so it has no rolling code of ours to keep in
  // order.
  RadioScheduler s;
  somfy_tx_job_t job = makeJob(REMOTE_A);
  job.repeater = true;
  const uint32_t r = s.submit(job, false, 0, 0);
  const uint32_t stop = submit(s, REMOTE_A, 0, true);
  TEST_ASSERT_EQUAL_UINT32(stop, runNext(s));
  TEST_ASSERT_EQUAL_UINT32(r, runNext(s));
  discardEvents(s);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_jobs_go_out_in_the_order_they_came);
  RUN_TEST(test_tickets_are_distinct_and_set_in_the_job);
  RUN_TEST(test_nothing_waiting_sleeps_until_woken);
  RUN_TEST(test_stop_goes_ahead_of_other_remotes);
  RUN_TEST(test_stop_queues_behind_its_own_remote);
  RUN_TEST(test_stop_goes_ahead_once_its_remote_is_on_air);
  RUN_TEST(test_stops_go_out_in_the_order_they_came);
  RUN_TEST(test_second_stop_of_a_remote_stays_behind_the_first);
  RUN_TEST(test_normal_jobs_leave_the_reserved_slots);
  RUN_TEST(test_stops_use_the_reserved_slots);
  RUN_TEST(test_a_refused_job_has_no_ticket);
  RUN_TEST(test_taking_a_job_frees_its_slot);
  RUN_TEST(test_delayed_job_waits_until_due);
  RUN_TEST(test_delayed_job_holds_up_the_jobs_behind_it);
  RUN_TEST(test_stop_overtakes_a_delayed_job_of_another_remote);
  RUN_TEST(test_delay_counts_from_the_job_ahead_going_off_air);
  RUN_TEST(test_delay_ignores_a_job_that_ended_before_it_was_queued);
  RUN_TEST(test_delay_after_a_job_ended_across_millis_wrap);
  RUN_TEST(test_stop_waits_behind_its_remotes_delayed_job);
  RUN_TEST(test_delay_across_millis_wrap);
  RUN_TEST(test_job_reports_started_then_finished);
  RUN_TEST(test_cut_short_job_reports_aborted);
  RUN_TEST(test_flush_drops_every_waiting_job_once);
  RUN_TEST(test_flush_cuts_short_the_job_on_air);
  RUN_TEST(test_job_flushed_before_it_starts_is_dropped);
  RUN_TEST(test_every_job_gets_one_outcome_across_a_flush);
  RUN_TEST(test_no_outcome_is_lost_when_events_arent_collected);
  RUN_TEST(test_flush_reports_every_drop_when_events_arent_collected);
  RUN_TEST(test_unknown_ticket_is_ignored);
  RUN_TEST(test_suspend_flushes_and_refuses_jobs);
  RUN_TEST(test_resume_needs_every_reason_cleared);
  RUN_TEST(test_repeater_frames_report_nothing);
  RUN_TEST(test_repeater_frame_doesnt_hold_back_a_stop);
  return UNITY_END();
}
