// Host regression for src/usb_in_pacer.cpp. Built by tests/test_usb_in_pacer.py.
//
// Besides unit checks, a small simulator models the real pipeline: the host
// polls an interrupt IN endpoint on a fixed grid (optionally with clock
// drift), the firmware main loop runs in passes of random length, Bluetooth
// input arrives periodically and is only seen at the next pass, an armed
// packet cannot be replaced, and a completion is only observed on the pass
// after the poll. Latency is measured per Bluetooth sample as the time until
// the host received a report at least as new as that sample.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "usb_in_pacer.h"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace {

using Policy = UsbInPacer::Policy;

struct Rng {
    uint32_t s;
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    uint32_t range(uint32_t lo, uint32_t hi) { return hi > lo ? lo + next() % (hi - lo + 1) : lo; }
};

struct Params {
    Policy policy = Policy::JustInTime;
    bool send_duplicates = true;
    uint32_t interval_us = 1000;    // advertised bInterval
    double host_period_us = 1000.0; // real host poll period (drift)
    uint32_t loop_min_us = 10;      // main-loop pass length range
    uint32_t loop_max_us = 120;
    uint32_t bt_period_us = 1250;   // Bluetooth input period
    uint32_t bt_jitter_us = 200;
    uint64_t duration_us = 10'000'000;
    uint32_t seed = 1;
};

struct Result {
    double avg_latency_us = 0;
    uint64_t max_latency_us = 0;
    uint32_t polls = 0;
    uint32_t deliveries = 0;
    UsbInPacer::Stats stats{};
    double miss_rate() const {
        return stats.scheduled ? static_cast<double>(stats.misses) / stats.scheduled : 0.0;
    }
    double delivery_rate() const {
        return polls ? static_cast<double>(deliveries) / polls : 0.0;
    }
};

Result simulate(const Params& p) {
    UsbInPacer pacer;
    pacer.configure(p.policy, p.interval_us, p.send_duplicates);
    Rng rng{p.seed};

    std::vector<uint64_t> arrivals;
    int seen = -1;           // newest sample visible to the firmware
    int armed_sample = -1;   // newest sample inside the armed packet
    int delivered = -1;      // newest sample the host has received
    bool armed = false;
    bool fresh = false;
    bool completion = false;

    Result r;
    double latency_sum = 0;
    uint64_t latency_count = 0;
    const uint64_t host_phase = 137;
    uint64_t poll_index = 0;
    auto poll_time = [&](uint64_t k) {
        return host_phase + static_cast<uint64_t>(static_cast<double>(k) * p.host_period_us);
    };
    uint64_t next_bt = 300;

    auto service = [&](uint64_t now) {
        if (armed || !pacer.want_arm(now, fresh)) return;
        armed = true;
        armed_sample = seen;
        fresh = false;
        pacer.on_armed(now);
    };

    uint64_t t = 0;
    while (t < p.duration_us) {
        const uint64_t next_t = t + rng.range(p.loop_min_us, p.loop_max_us);
        // The firmware acted at t; the host polls while this pass runs.
        while (poll_time(poll_index) < next_t) {
            const uint64_t pt = poll_time(poll_index++);
            if (pt < t) continue;
            ++r.polls;
            if (!armed) continue;
            armed = false;
            completion = true;
            ++r.deliveries;
            for (int j = delivered + 1; j <= armed_sample; ++j) {
                const uint64_t latency = pt - arrivals[j];
                latency_sum += static_cast<double>(latency);
                ++latency_count;
                if (latency > r.max_latency_us) r.max_latency_us = latency;
            }
            if (armed_sample > delivered) delivered = armed_sample;
        }
        while (next_bt < next_t) {
            arrivals.push_back(next_bt);
            next_bt += p.bt_period_us - p.bt_jitter_us / 2 + rng.range(0, p.bt_jitter_us);
        }
        t = next_t;
        // Pass at t, in firmware order: cyw43_arch_poll (BT input), tud_task
        // (IN completion), interrupt_loop.
        if (static_cast<int>(arrivals.size()) - 1 > seen) {
            seen = static_cast<int>(arrivals.size()) - 1;
            fresh = true;
            service(t);
        }
        if (completion) {
            completion = false;
            pacer.on_complete(t);
            service(t);
        }
        service(t);
    }

    r.avg_latency_us = latency_count ? latency_sum / static_cast<double>(latency_count) : 0;
    r.stats = pacer.stats();
    return r;
}

void print(const char* name, const Result& r) {
    std::printf("%-34s avg %7.1f us  max %6llu us  rate %.3f  miss %.4f  margin %u us\n",
                name, r.avg_latency_us, static_cast<unsigned long long>(r.max_latency_us),
                r.delivery_rate(), r.miss_rate(), r.stats.margin_us);
}

Params keep_full(Params p) {
    // The original fixed-rate behaviour: re-arm with the latest data as soon
    // as the endpoint is idle.
    p.policy = Policy::Immediate;
    p.send_duplicates = true;
    return p;
}

void unit_checks() {
    // Immediate: fresh-only never arms without fresh data.
    UsbInPacer a;
    a.configure(Policy::Immediate, 1000, false);
    CHECK(!a.want_arm(0, false));
    CHECK(a.want_arm(0, true));

    // Just-in-time with duplicates: arms at once until the phase is known.
    UsbInPacer b;
    b.configure(Policy::JustInTime, 1000, true);
    CHECK(b.want_arm(10, false));
    b.on_armed(10);
    b.on_complete(1000); // host poll observed at 1000
    // Next poll predicted at 2000, margin 250: closed until 1750.
    CHECK(!b.want_arm(1200, false));
    CHECK(!b.want_arm(1749, false));
    CHECK(b.want_arm(1750, false));
    // A long silence makes the phase stale: arm at once again.
    CHECK(b.want_arm(1000 + UsbInPacer::kPhaseStaleUs + 1, false));
    // reset() forgets the phase.
    b.on_complete(5000);
    b.reset();
    CHECK(b.want_arm(5100, false));

    // Fresh-only just-in-time waits for both data and the window.
    UsbInPacer c;
    c.configure(Policy::JustInTime, 4000, false);
    c.on_complete(0);
    CHECK(!c.want_arm(3800, false));
    CHECK(!c.want_arm(1000, true));
    CHECK(c.want_arm(3800, true));

    // Reconfiguring clamps the margin to half an interval.
    UsbInPacer d;
    d.configure(Policy::JustInTime, 400, true);
    CHECK(d.stats().margin_us == 200);
}

} // namespace

int main() {
    unit_checks();

    // 1000 Hz fixed rate (mode 2).
    Params p1k;
    const Result full_1k = simulate(keep_full(p1k));
    const Result jit_1k = simulate(p1k);
    print("1000 Hz keep-full (old)", full_1k);
    print("1000 Hz just-in-time", jit_1k);
    CHECK(full_1k.avg_latency_us > 1300);
    CHECK(jit_1k.avg_latency_us < full_1k.avg_latency_us - 500);
    CHECK(jit_1k.delivery_rate() > 0.99); // constant rate kept
    CHECK(jit_1k.miss_rate() < 0.01);

    // Real-time (mode 3): fresh-only, armed on arrival.
    Params rt = p1k;
    rt.policy = Policy::Immediate;
    rt.send_duplicates = false;
    const Result rt_1k = simulate(rt);
    print("real-time immediate", rt_1k);
    CHECK(rt_1k.avg_latency_us < 750);

    // 250 Hz fixed rate (mode 0).
    Params p250 = p1k;
    p250.interval_us = 4000;
    p250.host_period_us = 4000.0;
    const Result full_250 = simulate(keep_full(p250));
    const Result jit_250 = simulate(p250);
    print("250 Hz keep-full (old)", full_250);
    print("250 Hz just-in-time", jit_250);
    CHECK(full_250.avg_latency_us > 5000);
    CHECK(jit_250.avg_latency_us < full_250.avg_latency_us / 2);
    CHECK(jit_250.delivery_rate() > 0.99);
    CHECK(jit_250.miss_rate() < 0.01);

    // Host clock drift in both directions (USB allows +-500 ppm).
    for (const double period : {1000.4, 999.6}) {
        Params drift = p1k;
        drift.host_period_us = period;
        const Result r = simulate(drift);
        print(period > 1000 ? "1000 Hz host +400 ppm" : "1000 Hz host -400 ppm", r);
        CHECK(r.miss_rate() < 0.01);
        CHECK(r.delivery_rate() > 0.99);
        CHECK(r.avg_latency_us < full_1k.avg_latency_us - 400);
    }

    // A main loop slower than the initial margin: the margin must grow.
    Params slow = p1k;
    slow.loop_min_us = 50;
    slow.loop_max_us = 400;
    const Result slow_full = simulate(keep_full(slow));
    const Result slow_jit = simulate(slow);
    print("slow loop keep-full (old)", slow_full);
    print("slow loop just-in-time", slow_jit);
    CHECK(slow_jit.stats.margin_us > UsbInPacer::kInitialMarginUs);
    CHECK(slow_jit.miss_rate() < 0.05);
    CHECK(slow_jit.avg_latency_us < slow_full.avg_latency_us);

    // A fast main loop: the margin shrinks over time.
    Params fast = p1k;
    fast.loop_min_us = 5;
    fast.loop_max_us = 30;
    fast.duration_us = 60'000'000;
    const Result fast_jit = simulate(fast);
    print("fast loop just-in-time (60 s)", fast_jit);
    CHECK(fast_jit.stats.margin_us < UsbInPacer::kInitialMarginUs);
    CHECK(fast_jit.miss_rate() < 0.01);

    // XInput fixed rate: fresh-only, just-in-time vs. arm-on-arrival, with
    // Bluetooth input faster than the 250 Hz endpoint.
    Params x250 = p250;
    x250.send_duplicates = false;
    x250.bt_period_us = 1000;
    Params x250_immediate = x250;
    x250_immediate.policy = Policy::Immediate;
    const Result xi = simulate(x250_immediate);
    const Result xj = simulate(x250);
    print("XInput 250 Hz arm-on-arrival", xi);
    print("XInput 250 Hz just-in-time", xj);
    CHECK(xj.avg_latency_us < xi.avg_latency_us - 1000);

    std::printf("usb_in_pacer: all checks passed\n");
    return 0;
}
