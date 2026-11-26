#ifndef DS5_BRIDGE_USB_IN_PACER_H
#define DS5_BRIDGE_USB_IN_PACER_H

#include <cstdint>

// Decides when to arm a single-packet interrupt IN transfer.
//
// The RP2040/RP2350 device controller copies an IN packet into its DPRAM when
// the transfer is armed, and an armed transfer cannot be replaced. The host
// receives exactly what was armed, at its next poll. Re-arming as soon as the
// previous report is taken ("keep the endpoint full") therefore makes every
// report one full polling interval old by the time the host reads it.
//
//   Immediate   Arm as soon as there is something to send. With fresh-only
//               reports and a 1 ms endpoint this is the real-time mode.
//   JustInTime  Arm shortly before the predicted next host poll, with the
//               newest data. Used by the fixed polling-rate modes.
//
// The host poll phase is learned from IN completions. A completion is only
// seen on the main-loop pass after the actual poll, so the earliest
// observation is trusted, with a small leak to follow clock drift. The margin
// before the predicted poll grows when a scheduled arm misses its poll and
// slowly shrinks while arms keep landing in time.
//
// Not thread safe: call everything from the same context (core0 main loop).
class UsbInPacer {
public:
    enum class Policy : uint8_t {
        Immediate,
        JustInTime,
    };

    struct Stats {
        uint32_t arms;      // transfers armed
        uint32_t scheduled; // just-in-time arms whose data was ready before the window
        uint32_t misses;    // scheduled arms that missed the predicted poll
        uint32_t margin_us; // current arm margin before the predicted poll
    };

    static constexpr uint32_t kInitialMarginUs = 250;
    static constexpr uint32_t kMinMarginUs = 50;
    static constexpr uint32_t kMarginStepUpUs = 50;
    static constexpr uint32_t kMarginStepDownUs = 10;
    static constexpr uint32_t kHitsPerStepDown = 2000;
    static constexpr uint32_t kLeakUs = 1;
    static constexpr uint32_t kPhaseStaleUs = 100000;

    // Apply the current polling mode. Changing any parameter forgets the phase.
    // send_duplicates keeps a constant report rate by re-sending the last state
    // when no fresh data arrived.
    void configure(Policy policy, uint32_t interval_us, bool send_duplicates);

    // Forget the poll phase, e.g. after a bus reset, suspend or reconfiguration.
    void reset();

    // Call only while the endpoint is idle. fresh = new data since the last arm.
    bool want_arm(uint64_t now_us, bool fresh);

    // Call after the transfer was successfully armed.
    void on_armed(uint64_t now_us);

    // Call when the IN transfer completed, i.e. the host took the report.
    void on_complete(uint64_t now_us);

    Stats stats() const;

private:
    bool phase_usable(uint64_t now_us) const;
    uint64_t next_poll_after(uint64_t now_us) const;
    uint32_t max_margin_us() const;

    Policy policy_ = Policy::Immediate;
    uint32_t interval_us_ = 1000;
    bool send_duplicates_ = false;

    bool phase_valid_ = false;
    uint64_t last_poll_us_ = 0; // estimated time of the most recent host poll

    bool pending_ = false;
    uint64_t pending_since_us_ = 0; // when data first waited for an idle endpoint

    bool armed_ = false;
    bool scheduled_ = false;
    uint64_t armed_for_us_ = 0; // predicted poll the armed transfer targets

    uint32_t margin_us_ = kInitialMarginUs;
    uint32_t hits_ = 0;
    uint32_t arms_ = 0;
    uint32_t scheduled_arms_ = 0;
    uint32_t misses_ = 0;
};

#endif // DS5_BRIDGE_USB_IN_PACER_H
