/*
 * Link statistics - cumulative media counters for the link quality window
 *
 * Process-wide so the counters outlive BtStackTransport instances (BTstack
 * can be re-created while the window is open). Writers: the BTstack thread
 * (packets sent), the encode thread (queue drops) and the WASAPI callback
 * (capture drops). The GUI samples them once per second and works with
 * deltas, so nothing is ever reset.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef LINK_STATS_H
#define LINK_STATS_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

/* Capacity of BtStackTransport's media send queue, in packets */
constexpr uint32_t MEDIA_QUEUE_PACKETS = 64;
/* A gap between two media packets this long counts as a stall */
constexpr uint32_t STALL_GAP_MS = 200;

struct LinkStats {
    /* Media packets handed to L2CAP and their L2CAP payload bytes (RTP header,
     * media payload header and codec data) */
    std::atomic<uint64_t> packets_sent{0};
    std::atomic<uint64_t> bytes_sent{0};
    /* Packets lost before the radio: send queue full / L2CAP refused the packet */
    std::atomic<uint64_t> queue_drops{0};
    std::atomic<uint64_t> send_errors{0};
    /* PCM frames captured from Windows, and those dropped because the encoder
     * fell behind (ring buffer full) */
    std::atomic<uint64_t> capture_frames{0};
    std::atomic<uint64_t> capture_dropped_frames{0};
    /* Instantaneous send queue fill (packets) */
    std::atomic<uint32_t> queue_depth{0};
    /* Gaps between media packets sent while streaming: the longest one (the
     * GUI resets it with exchange(0)) and the number of STALL_GAP_MS or more */
    std::atomic<uint32_t> max_send_gap_ms{0};
    std::atomic<uint64_t> stalls{0};
    /* The counters above when the current stream started, before its first
     * packet; stream_starts changes with every stream (written after them) */
    std::atomic<uint64_t> start_packets{0}, start_bytes{0}, start_dropped{0};
    std::atomic<uint64_t> start_capture_frames{0}, start_capture_dropped{0};
    std::atomic<uint32_t> stream_starts{0};
};

inline LinkStats &link_stats() {
    static LinkStats stats;
    return stats;
}

/* Radio state of the current link, read from the controller every few
 * seconds on the BTstack thread (BtStackTransport), shown by the GUI */
struct LinkRadio {
    bool has_rssi = false;
    int rssi = 0;              /* BR/EDR: relative to the golden receive range, not dBm */
    int afh_usable = -1;       /* channels the host classification leaves usable, -1 = none passed */
    std::string afh_avoided;   /* Wi-Fi channels passed as bad, "6, 11" */
    int afh_in_use = -1;       /* channels the link hops over (Read AFH Channel Map), -1 = unknown */
    int role = -1;             /* this PC on the link: 0 = central, 1 = peripheral, -1 = unknown */
};

inline std::mutex &link_radio_mutex() {
    static std::mutex m;
    return m;
}

inline LinkRadio &link_radio_unlocked() {
    static LinkRadio radio;
    return radio;
}

inline LinkRadio link_radio() {
    std::lock_guard<std::mutex> lock(link_radio_mutex());
    return link_radio_unlocked();
}

#endif /* LINK_STATS_H */
