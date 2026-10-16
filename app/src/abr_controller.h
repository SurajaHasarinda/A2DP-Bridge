/*
 * Stepped adaptive bit rate for AAC and SBC (LDAC and LHDC V5 have their own).
 *
 * The send queue shows when the radio falls behind: its backlog grows, and
 * past the latency mode's limit packets are dropped. Then the encoder steps
 * down to a lower bitrate (fewer bytes, more room for retransmissions); after
 * a long calm it steps back up, one step at a time.
 *
 *   step down  backlog >= half the queue limit (20 ms at least), or new drops;
 *              at most once a second
 *   step up    backlog <= 10 ms and no drops for 10 s
 */

#ifndef ABR_CONTROLLER_H
#define ABR_CONTROLLER_H

#include <cstdint>

class StepAbr {
public:
    void reset(int steps, uint32_t queue_limit_ms, uint64_t drops, uint64_t now_ms) {
        steps_ = steps;
        step_ = 0;
        down_ms_ = queue_limit_ms / 2 > 20 ? queue_limit_ms / 2 : 20;
        drops_ = drops;
        last_change_ = quiet_since_ = now_ms;
    }

    /* Call about every 100 ms; returns the step the encoder should use */
    int update(uint32_t backlog_ms, uint64_t drops, uint64_t now_ms) {
        const bool new_drops = drops != drops_;
        drops_ = drops;
        if (backlog_ms >= down_ms_ || new_drops) {
            quiet_since_ = now_ms;
            if (step_ + 1 < steps_ && now_ms - last_change_ >= 1000) {
                step_++;
                last_change_ = now_ms;
            }
        } else if (backlog_ms > QUIET_MS) {
            quiet_since_ = now_ms;
        } else if (step_ > 0 && now_ms - quiet_since_ >= CALM_MS && now_ms - last_change_ >= CALM_MS) {
            step_--;
            last_change_ = quiet_since_ = now_ms;
        }
        return step_;
    }

    int step() const { return step_; }

private:
    static constexpr uint32_t QUIET_MS = 10;
    static constexpr uint64_t CALM_MS = 10000;
    int steps_ = 1, step_ = 0;
    uint32_t down_ms_ = 100;
    uint64_t drops_ = 0, last_change_ = 0, quiet_since_ = 0;
};

#endif /* ABR_CONTROLLER_H */
