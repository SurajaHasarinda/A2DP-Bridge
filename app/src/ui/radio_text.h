/*
 * Radio state as text for the link quality windows
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef RADIO_TEXT_H
#define RADIO_TEXT_H

#include "link_stats.h"
#include "ui_util.h"

/* "RSSI +0 dB vs. golden range, AFH 45/79 ch (avoiding Wi-Fi 6, 11)" */
inline QString radio_text(bool has_rssi, int rssi, int afh_in_use, const std::string &avoided) {
    QString t;
    if (has_rssi) t = fmt("radio.rssi", rssi);
    if (afh_in_use >= 0) {
        if (!t.isEmpty()) t += U("radio.separator");
        t += fmt("radio.afh", afh_in_use);
    }
    if (!avoided.empty()) t += fmt("radio.afh_avoid", avoided);
    return t.isEmpty() ? QStringLiteral("-") : t;
}

inline QString radio_text(const LinkRadio &r) {
    return radio_text(r.has_rssi, r.rssi, r.afh_in_use, r.afh_usable >= 0 ? r.afh_avoided : std::string());
}

#endif /* RADIO_TEXT_H */
