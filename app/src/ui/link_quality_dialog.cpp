/*
 * Link quality window implementation
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "link_quality_dialog.h"

#include "link_stats.h"
#include "main_window.h"
#include "radio_text.h"
#include "theme.h"
#include "ui_util.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <windows.h>

namespace {

const char *DIALOG_NAME = "a2dpwb_link_quality";

/* Bluetooth BR/EDR baseband: 625 us n_slots. The best ACL packet type on an EDR
 * 3 Mbps link is 3-DH5 (5 n_slots, up to 1021 bytes); each packet is followed
 * by one slot for the receiver's reply. */
const double SLOT_SECONDS = 625e-6;
const uint32_t L2CAP_HEADER_BYTES = 4;
struct AclPacketType { const char *name; uint32_t max_bytes; uint32_t n_slots; };
const AclPacketType EDR3_TYPES[] = {{"3-DH1", 83, 1}, {"3-DH3", 552, 3}, {"3-DH5", 1021, 5}};
const AclPacketType &LARGEST_TYPE = EDR3_TYPES[2];
/* 1021 bytes every 6 n_slots: 2178 kbps */
const double THEORETICAL_MAX_KBPS = LARGEST_TYPE.max_bytes * 8.0 / ((LARGEST_TYPE.n_slots + 1) * SLOT_SECONDS) / 1000.0;

/* Slots (including reply n_slots) one ACL payload of acl_bytes takes on air, and the packet type used */
uint32_t airtime_slots(uint32_t acl_bytes, const char **type_name) {
    for (const auto &t : EDR3_TYPES) {
        if (acl_bytes <= t.max_bytes) {
            *type_name = t.name;
            return t.n_slots + 1;
        }
    }
    /* Split over several 3-DH5 packets */
    uint32_t n = (acl_bytes + LARGEST_TYPE.max_bytes - 1) / LARGEST_TYPE.max_bytes;
    *type_name = LARGEST_TYPE.name;
    return n * (LARGEST_TYPE.n_slots + 1);
}

int gauge_value(double percent) { return percent < 0 ? 0 : percent > 100 ? 100 : static_cast<int>(percent + 0.5); }

void set_flag(QWidget *w, const char *prop, bool on) {
    w->setProperty(prop, on);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

} // namespace

void LinkQualityDialog::show_for(MainWindow *window) {
    if (auto *existing = window->findChild<LinkQualityDialog *>(DIALOG_NAME)) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto *dlg = new LinkQualityDialog(&window->backend(), window);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

LinkQualityDialog::Sample LinkQualityDialog::take_sample() {
    const LinkStats &s = link_stats();
    Sample r;
    r.tick_ms = GetTickCount64();
    r.packets = s.packets_sent.load(std::memory_order_relaxed);
    r.bytes = s.bytes_sent.load(std::memory_order_relaxed);
    r.dropped = s.queue_drops.load(std::memory_order_relaxed) + s.send_errors.load(std::memory_order_relaxed);
    r.capture_frames = s.capture_frames.load(std::memory_order_relaxed);
    r.capture_dropped = s.capture_dropped_frames.load(std::memory_order_relaxed);
    return r;
}

LinkQualityDialog::LinkQualityDialog(Backend *backend, QWidget *parent)
    : QDialog(parent), backend_(backend), timer_(new QTimer(this)) {
    setObjectName(DIALOG_NAME);
    setWindowTitle(U("quality.title"));
    setMinimumWidth(520);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 14);
    root->setSpacing(10);

    idle_label_ = new QLabel(U("quality.not_streaming"), this);
    idle_label_->setProperty("warning", true);
    root->addWidget(idle_label_);

    auto section = [&](const char *title_key, QGridLayout **grid_out) {
        auto *title = new QLabel(U(title_key), this);
        title->setProperty("heading", true);
        root->addWidget(title);
        auto *card = new QFrame(this);
        card->setObjectName("card");
        auto *grid = new QGridLayout(card);
        grid->setContentsMargins(14, 10, 14, 10);
        grid->setHorizontalSpacing(16);
        grid->setVerticalSpacing(6);
        grid->setColumnStretch(1, 1);
        root->addWidget(card);
        *grid_out = grid;
        return card;
    };
    auto add_row = [&](QGridLayout *grid, const char *label_key) {
        const int row = grid->rowCount();
        auto *label = new QLabel(U(label_key), grid->parentWidget());
        label->setProperty("secondary", true);
        auto *value = new QLabel("-", grid->parentWidget());
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(label, row, 0, Qt::AlignTop);
        grid->addWidget(value, row, 1);
        return value;
    };
    auto add_gauge = [&](QGridLayout *grid) {
        auto *bar = new QProgressBar(grid->parentWidget());
        bar->setRange(0, 100);
        bar->setTextVisible(false);
        grid->addWidget(bar, grid->rowCount(), 1);
        return bar;
    };
    auto add_help = [&](const char *key) {
        auto *help = new QLabel(U(key), this);
        help->setProperty("muted", true);
        help->setWordWrap(true);
        root->addWidget(help);
    };

    QGridLayout *g;
    section("quality.section_stream", &g);
    codec_value_ = add_row(g, "quality.codec");

    section("quality.section_send", &g);
    bitrate_value_ = add_row(g, "quality.bitrate");
    packets_value_ = add_row(g, "quality.packets");
    queue_value_ = add_row(g, "quality.queue");
    radio_value_ = add_row(g, "quality.radio");
    radio_value_->setWordWrap(true);

    section("quality.section_band", &g);
    theory_value_ = add_row(g, "quality.theory");
    theory_gauge_ = add_gauge(g);
    airtime_value_ = add_row(g, "quality.airtime");
    airtime_gauge_ = add_gauge(g);
    add_help("quality.band_help");

    section("quality.section_loss", &g);
    sent_value_ = add_row(g, "quality.sent_total");
    dropped_value_ = add_row(g, "quality.dropped");
    capture_value_ = add_row(g, "quality.capture_dropped");
    add_help("quality.loss_help");

    auto *bar = new QHBoxLayout;
    auto *reset = new QPushButton(U("quality.reset"), this);
    reset->setAutoDefault(false);
    connect(reset, &QPushButton::clicked, this, [this] {
        baseline_ = take_sample();
        refresh();
    });
    bar->addWidget(reset);
    bar->addStretch(1);
    auto *close = new QPushButton(U("modal.close"), this);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    bar->addWidget(close);
    root->addLayout(bar);

    baseline_ = take_sample();
    prev_ = baseline_;
    connect(timer_, &QTimer::timeout, this, &LinkQualityDialog::refresh);
    refresh();
    timer_->start(1000);
}

void LinkQualityDialog::refresh() {
    const Sample now = take_sample();
    const auto &info = backend_->stream_info();
    const bool streaming = backend_->state() == A2dpService::State::Streaming;

    /* ---- Stream ---- */
    codec_value_->setText(streaming && !info.codec.empty()
        ? fmt("quality.codec_value", info.codec, info.sample_rate / 1000.0, (unsigned)info.channels,
              (unsigned)info.bitrate_kbps)
        : QString("-"));

    /* ---- Last second ---- */
    const double dt = have_prev_ ? (now.tick_ms - prev_.tick_ms) / 1000.0 : 0.0;
    const uint64_t d_packets = now.packets - prev_.packets;
    const uint64_t d_bytes = now.bytes - prev_.bytes;
    const bool have_rate = streaming && dt > 0.2 && d_packets > 0;
    idle_label_->setVisible(!streaming);

    if (have_rate) {
        const double pps = d_packets / dt;
        const double avg_bytes = static_cast<double>(d_bytes) / d_packets;
        const double kbps = d_bytes * 8.0 / dt / 1000.0;
        bitrate_value_->setText(fmt("quality.bitrate_value", kbps));
        packets_value_->setText(fmt("quality.packets_value", pps, avg_bytes));

        /* Bandwidth: each L2CAP payload gets a 4-byte L2CAP header in the ACL packet */
        const double acl_bytes = avg_bytes + L2CAP_HEADER_BYTES;
        const double acl_kbps = pps * acl_bytes * 8.0 / 1000.0;
        const double theory_pct = 100.0 * acl_kbps / THEORETICAL_MAX_KBPS;
        theory_value_->setText(fmt("quality.theory_value", THEORETICAL_MAX_KBPS, theory_pct));
        theory_gauge_->setValue(gauge_value(theory_pct));

        const char *type_name = "";
        const uint32_t n_slots = airtime_slots(static_cast<uint32_t>(acl_bytes + 0.5), &type_name);
        const double airtime_pct = 100.0 * pps * n_slots * SLOT_SECONDS;
        airtime_value_->setText(fmt("quality.airtime_value", airtime_pct, type_name, (unsigned)n_slots));
        airtime_gauge_->setValue(gauge_value(airtime_pct));
        set_flag(airtime_value_, "error", airtime_pct >= 80.0);
        set_flag(airtime_gauge_, "warn", airtime_pct >= 80.0);
    } else {
        bitrate_value_->setText("-");
        packets_value_->setText("-");
        theory_value_->setText("-");
        theory_gauge_->setValue(0);
        airtime_value_->setText("-");
        airtime_gauge_->setValue(0);
        set_flag(airtime_value_, "error", false);
        set_flag(airtime_gauge_, "warn", false);
    }
    radio_value_->setText(streaming ? radio_text(link_radio()) : QString("-"));
    queue_value_->setText(streaming ? fmt("quality.queue_value", (unsigned)link_stats().queue_depth.load(std::memory_order_relaxed),
                                          (unsigned)MEDIA_QUEUE_PACKETS)
                                    : QString("-"));

    /* ---- Losses since opened / reset ---- */
    const uint64_t sent = now.packets - baseline_.packets;
    const uint64_t dropped = now.dropped - baseline_.dropped;
    const uint64_t cap_frames = now.capture_frames - baseline_.capture_frames;
    const uint64_t cap_dropped = now.capture_dropped - baseline_.capture_dropped;
    sent_value_->setText(QString::number(sent));
    const double drop_pct = (sent + dropped) ? 100.0 * dropped / (sent + dropped) : 0.0;
    dropped_value_->setText(fmt_lit("%llu (%.2f%%)", (unsigned long long)dropped, drop_pct));
    set_flag(dropped_value_, "error", dropped > 0);
    const double cap_ms = info.source_sample_rate ? 1000.0 * cap_dropped / info.source_sample_rate : 0.0;
    const double cap_pct = cap_frames ? 100.0 * cap_dropped / cap_frames : 0.0;
    capture_value_->setText(fmt_lit("%.0f ms (%.2f%%)", cap_ms, cap_pct));
    set_flag(capture_value_, "error", cap_dropped > 0);

    prev_ = now;
    have_prev_ = true;
}
