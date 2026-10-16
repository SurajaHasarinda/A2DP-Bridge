/*
 * Latency mode of a profile: how much audio may wait in the send queue, how
 * much audio one media packet carries, and which codec Auto prefers.
 *
 *   standard  200 ms queue, packets filled up to the size limit, best quality first
 *   low       100 ms queue, at most 10 ms of audio per packet, fast codecs first
 *   lowest     60 ms queue, at most  5 ms of audio per packet, fast codecs first
 *
 * Lower modes cut the delay; on a weak link they skip sooner instead of
 * catching up.
 */

#ifndef LATENCY_MODE_H
#define LATENCY_MODE_H

#include <cstdint>
#include <string>

struct LatencyMode {
    uint32_t max_queue_ms;   /* send queue backlog limit (BtStackTransport::send_media) */
    uint32_t max_packet_ms;  /* audio per media packet, 0 = no limit beyond the size limit */
    bool fast_codecs_first;  /* Auto: aptX LL > aptX > SBC > AAC > LHDC V5 > aptX HD > LDAC */
};

inline LatencyMode latency_mode(const std::string &name) {
    if (name == "lowest") return {60, 5, true};
    if (name == "low") return {100, 10, true};
    return {200, 0, false};
}

#endif /* LATENCY_MODE_H */
