#include "usb_in_pacer.h"

// The pacer runs on the input-report hot path; keep it out of XIP flash on the
// device. Host builds (tests) have no pico.h and compile it normally.
#if __has_include("pico.h")
#include "pico.h"
#define USB_IN_PACER_RAM __not_in_flash("usb_in_pacer")
#else
#define USB_IN_PACER_RAM
#endif

uint32_t USB_IN_PACER_RAM UsbInPacer::max_margin_us() const {
    return interval_us_ / 2;
}

void USB_IN_PACER_RAM UsbInPacer::configure(Policy policy, uint32_t interval_us,
                                            bool send_duplicates) {
    if (interval_us == 0) interval_us = 1000;
    if (policy == policy_ && interval_us == interval_us_ &&
        send_duplicates == send_duplicates_) {
        return;
    }
    policy_ = policy;
    interval_us_ = interval_us;
    send_duplicates_ = send_duplicates;
    if (margin_us_ > max_margin_us()) margin_us_ = max_margin_us();
    reset();
}

void USB_IN_PACER_RAM UsbInPacer::reset() {
    phase_valid_ = false;
    pending_ = false;
    armed_ = false;
    scheduled_ = false;
    hits_ = 0;
}

bool USB_IN_PACER_RAM UsbInPacer::phase_usable(uint64_t now_us) const {
    // Without completions the phase drifts with the host/device clock offset,
    // so an old estimate is not trusted.
    return phase_valid_ && now_us - last_poll_us_ <= kPhaseStaleUs;
}

uint64_t USB_IN_PACER_RAM UsbInPacer::next_poll_after(uint64_t now_us) const {
    // Only called with a usable phase, so the distance fits in 32 bits.
    const uint32_t since = static_cast<uint32_t>(now_us - last_poll_us_);
    const uint32_t polls = since / interval_us_ + 1;
    return last_poll_us_ + static_cast<uint64_t>(polls) * interval_us_;
}

bool USB_IN_PACER_RAM UsbInPacer::want_arm(uint64_t now_us, bool fresh) {
    if (!fresh && !send_duplicates_) return false;
    if (!pending_) {
        pending_ = true;
        pending_since_us_ = now_us;
    }
    // Immediate always arms; just-in-time arms immediately until it has seen
    // a completion to learn the poll phase from.
    if (policy_ == Policy::Immediate || !phase_usable(now_us)) return true;
    return now_us + margin_us_ >= next_poll_after(now_us);
}

void USB_IN_PACER_RAM UsbInPacer::on_armed(uint64_t now_us) {
    ++arms_;
    armed_ = true;
    scheduled_ = false;
    if (policy_ == Policy::JustInTime && phase_usable(now_us)) {
        armed_for_us_ = next_poll_after(now_us);
        // Only arms whose data was already waiting when the window opened say
        // anything about the margin. Data that shows up inside the window is
        // armed at once and may legitimately be too late for that poll.
        scheduled_ = pending_since_us_ + margin_us_ <= armed_for_us_;
        if (scheduled_) ++scheduled_arms_;
    }
    pending_ = false;
}

void USB_IN_PACER_RAM UsbInPacer::on_complete(uint64_t now_us) {
    if (!phase_valid_ || now_us - last_poll_us_ > kPhaseStaleUs) {
        last_poll_us_ = now_us;
        phase_valid_ = true;
    } else {
        const uint32_t since = static_cast<uint32_t>(now_us - last_poll_us_);
        const uint32_t polls = (since + interval_us_ / 2) / interval_us_;
        if (polls == 0) {
            // Closer than half an interval to the previous poll: the estimate
            // was late (or the host polls faster than advertised). Re-sync.
            last_poll_us_ = now_us;
        } else {
            // The poll happened at or before this observation. Keep the
            // earlier of the observation and the prediction; the leak lets the
            // estimate follow a host clock that runs slightly slower.
            const uint64_t predicted =
                last_poll_us_ + static_cast<uint64_t>(polls) * interval_us_ + kLeakUs;
            last_poll_us_ = now_us < predicted ? now_us : predicted;
        }
    }

    if (armed_ && scheduled_) {
        if (last_poll_us_ > armed_for_us_ + interval_us_ / 2) {
            // Taken one poll later than targeted: arm earlier from now on.
            ++misses_;
            hits_ = 0;
            margin_us_ += kMarginStepUpUs;
            if (margin_us_ > max_margin_us()) margin_us_ = max_margin_us();
        } else if (++hits_ >= kHitsPerStepDown) {
            hits_ = 0;
            margin_us_ = margin_us_ >= kMinMarginUs + kMarginStepDownUs
                             ? margin_us_ - kMarginStepDownUs
                             : kMinMarginUs;
        }
    }
    armed_ = false;
    scheduled_ = false;
}

UsbInPacer::Stats UsbInPacer::stats() const {
    return Stats{arms_, scheduled_arms_, misses_, margin_us_};
}
