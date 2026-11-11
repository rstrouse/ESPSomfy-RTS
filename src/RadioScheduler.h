#ifndef RADIO_SCHEDULER_H
#define RADIO_SCHEDULER_H
#include <stdint.h>
#include "TxBuffer.h"

// Reasons the radio must not transmit.  TX resumes once every reason is cleared.
#define TX_SUSPEND_SCAN 0x01
#define TX_SUSPEND_OTA  0x02
// No job is waiting so the radio task sleeps until it is woken.
#define RADIO_WAIT_FOREVER 0xFFFFFFFFu

// The radio jobs waiting for, or on, the air (see GLOSSARY.md).  It decides
// their order, when a delayed job is due, what a flush or suspend drops, and
// reports the outcome of every job.  Kept free of FreeRTOS and the radio so it
// can be unit tested on the PC (pio test -e native).
//
// Not thread safe.  The caller serializes every call and passes the time in.
class RadioScheduler {
  public:
    // Queues a job.  Its ticket comes back and is also set in job.ticket, 0 when
    // the queue is full or suspended, or when outcomes aren't collected with
    // popEvent() fast enough to leave room for its own.  A priority job (a stop) may use the
    // reserved slots and goes ahead of every job queued at the back, unless a
    // job of its remote is already waiting: overtaking that would put the
    // rolling codes out of order.  A delayed job doesn't go on air until
    // delayMs after it was queued and after the job ahead of it ended, and holds
    // up the jobs queued behind it.
    uint32_t submit(somfy_tx_job_t &job, bool priority, uint16_t delayMs, uint32_t now);
    // Whether a normal (not priority) job would be taken right now.
    bool hasRoom() const;
    // For the radio task: takes the job to send next.  False when there is
    // none due, waitMs then says how long until there is (RADIO_WAIT_FOREVER
    // when nothing waits).  A job taken goes on air through start() and
    // ended().
    bool next(uint32_t now, somfy_tx_job_t &job, uint32_t &waitMs);
    // The job taken is about to go on air.  False when it was flushed since,
    // it is then dropped and must not be sent.
    bool start(uint32_t ticket, uint32_t now);
    // Whether the job on air should send its next frame.  A flush cuts it
    // short.
    bool keepSending(uint32_t ticket) const;
    // The job on air stopped, after every frame when complete.
    void ended(uint32_t ticket, bool complete, uint32_t now);
    // Drops every waiting job and cuts short the one on air.
    void flush(uint32_t now);
    // Refuses new jobs until every reason (TX_SUSPEND_*) is resumed.  Flushes.
    void suspend(uint8_t reason, uint32_t now);
    void resume(uint8_t reason);
    bool suspended() const { return this->suspendMask != 0; }
    // Takes the oldest job outcome not yet handed out.  Every job submitted
    // that isn't a repeater frame is started and then finished or aborted, or
    // dropped.
    bool popEvent(somfy_tx_event_t &evt);
  private:
    somfy_tx_job_t jobs[TX_JOB_QUEUE_SIZE];
    uint8_t count = 0;
    uint32_t lastTicket = 0;
    uint8_t suspendMask = 0;
    // The job taken by next() and not yet ended (0 for none), kept for its
    // events.
    uint32_t taken = 0;
    somfy_tx_job_t takenJob;
    bool takenFlushed = false;
    // When a job last went off air, for the delay of the job after it.
    uint32_t lastEnded = 0;
    bool endedOnce = false;
    somfy_tx_event_t events[TX_JOB_QUEUE_SIZE * 3];
    uint8_t eventHead = 0;
    uint8_t eventCount = 0;
    // Whether the outcomes of every job not yet ended, and of one more, still
    // fit in events.  A job is refused otherwise so no outcome is ever lost.
    bool outcomesFit() const;
    // Whether a job of the remote is still waiting.
    bool waitingFor(uint32_t remoteAddress) const;
    void remove(uint8_t i);
    void post(tx_event_t type, const somfy_tx_job_t &job, uint32_t at);
};
#endif
