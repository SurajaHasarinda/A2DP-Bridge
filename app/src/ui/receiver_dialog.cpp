/*
 * Peer receiver test implementation
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "receiver_dialog.h"

#include "btstack_transport.h"
#include "link_stats.h"
#include "main_window.h"
#include "profile_manager.h"
#include "radio_text.h"
#include "theme.h"
#include "ui_util.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <windows.h>

namespace {

const char *DIALOG_NAME = "a2dpwb_receiver";

/* The receiver reports every second; older data means it stopped */
const uint64_t STATS_STALE_MS = 3000;

/* Codec choice order = ProfileManager::index_to_codec() */
const char *CODEC_LABELS[] = {nullptr /* codec.auto */, "LDAC", "aptX HD", "aptX LL", "aptX", "SBC", "AAC"};
const char *QUALITY_KEYS[] = {"quality.high", "quality.standard", "quality.mobile"};
/* Choice index -> ConnectionProfile values (0 = automatic) */
const uint32_t SAMPLE_RATES[] = {0, 44100, 48000, 88200, 96000};
const uint32_t BIT_DEPTHS[] = {0, 16, 24};
/* Receiver L2CAP MTU: 0 = as the receiver was started; 672 = BlueZ default,
 * 679 = smallest LDAC accepts, 895 / 1005 = common in headphones */
const uint32_t MTUS[] = {0, 672, 679, 895, 1005, 2048};
const uint32_t BUFFERS_MS[] = {0, 100, 200, 300, 500};

double pct(uint64_t part, uint64_t whole) { return whole ? 100.0 * part / whole : 0.0; }

const char *STEP_KEYS[] = {"receiver.step_discover", "receiver.step_stats", "receiver.step_configure",
                           "receiver.step_link", "receiver.step_codec", "receiver.step_audio"};
/* A step not confirmed within this long after it started is NG */
const uint64_t CONFIGURE_TIMEOUT_MS = 5000;
const uint64_t AUDIO_TIMEOUT_MS = 8000;
const int LOG_MAX_CHARS = 64 * 1024;

void set_flag(QWidget *w, const char *prop, bool on) {
    w->setProperty(prop, on);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

} // namespace

void ReceiverDialog::show_for(MainWindow *window) {
    if (auto *existing = window->findChild<ReceiverDialog *>(DIALOG_NAME)) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto *dlg = new ReceiverDialog(window);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

ReceiverDialog::Sample ReceiverDialog::take_sample() {
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

void ReceiverDialog::set_value(QLabel *t, const QString &text, bool bad) {
    if (t->text() != text) {
        t->setText(text);
        t->setToolTip(text);
    }
    set_flag(t, "error", bad);
}

ReceiverDialog::ReceiverDialog(MainWindow *window)
    : QDialog(window), window_(window), backend_(&window->backend()), timer_(new QTimer(this)) {
    setObjectName(DIALOG_NAME);
    setWindowTitle(U("receiver.title"));
    setMinimumWidth(720);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 14);
    root->setSpacing(10);

    auto heading = [&](const char *key) {
        auto *h = new QLabel(U(key), this);
        h->setProperty("heading", true);
        return h;
    };
    auto card = [&](QGridLayout **grid) {
        auto *c = new QFrame(this);
        c->setObjectName("card");
        auto *g = new QGridLayout(c);
        g->setContentsMargins(14, 10, 14, 10);
        g->setHorizontalSpacing(12);
        g->setVerticalSpacing(8);
        *grid = g;
        root->addWidget(c);
        return c;
    };

    auto *intro = new QLabel(U("receiver.intro"), this);
    intro->setProperty("muted", true);
    intro->setWordWrap(true);
    root->addWidget(intro);

    /* ---- Receiver: address (empty = find on the local network) ---- */
    auto *addr_row = new QHBoxLayout;
    auto *addr_label = new QLabel(U("receiver.address"), this);
    addr_label->setProperty("secondary", true);
    addr_row->addWidget(addr_label);
    const std::string saved = backend_->settings().remote_sink;
    addr_ = new QLineEdit(saved == REMOTE_SINK_AUTO ? QString() : U8(saved), this);
    addr_->setPlaceholderText(U("receiver.address_hint"));
    addr_->setToolTip(U("receiver.address_tooltip"));
    addr_row->addWidget(addr_, 1);
    connect_btn_ = new QPushButton(U("receiver.connect"), this);
    addr_row->addWidget(connect_btn_);
    root->addLayout(addr_row);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    root->addWidget(status_);

    /* ---- Measurement settings: what A2DPWB sends, how the receiver takes it ---- */
    QStringList codecs, qualities, rates, depths, mtus, buffers;
    for (const char *c : CODEC_LABELS) codecs << (c ? QString(c) : U("codec.auto"));
    for (const char *k : QUALITY_KEYS) qualities << U(k);
    for (uint32_t r : SAMPLE_RATES) rates << (r ? QString("%1 kHz").arg(r / 1000.0, 0, 'f', 1) : U("codec.auto"));
    for (uint32_t d : BIT_DEPTHS) depths << (d ? QString("%1 bit").arg(d) : U("codec.auto"));
    for (uint32_t m : MTUS) mtus << (m ? QString::number(m) : U("receiver.as_started"));
    for (uint32_t b : BUFFERS_MS) buffers << (b ? QString("%1 ms").arg(b) : U("receiver.as_started"));

    QGridLayout *sg;
    card(&sg);
    int row = 0, col = 0;
    auto head = [&](const char *key) {
        row++;
        col = 0;
        sg->addWidget(heading(key), row, col++);
    };
    auto choice = [&](const char *label_key, const QStringList &items, int selection) {
        auto *l = new QLabel(U(label_key), this);
        l->setProperty("secondary", true);
        auto *c = new QComboBox(this);
        c->addItems(items);
        c->setCurrentIndex(selection);
        sg->addWidget(l, row, col++, Qt::AlignRight);
        sg->addWidget(c, row, col++);
        return c;
    };
    row = -1;
    head("receiver.settings_sent");
    codec_ = choice("receiver.codec", codecs, 0);
    quality_ = choice("receiver.quality", qualities, 0);
    rate_ = choice("receiver.sample_rate", rates, 0);
    depth_ = choice("receiver.bit_depth", depths, 0);
    /* Second row: the audio source (the test tone keeps the stream steady) and AFH */
    row++;
    col = 1;
    source_ = choice("receiver.source", {U("receiver.source_tone"), U("receiver.source_system")}, 0);
    source_->setToolTip(U("receiver.source_tooltip"));
    const std::string afh_setting = backend_->settings().afh;
    const int afh_sel = afh_setting == "auto" ? 0 : (afh_setting == "off" || afh_setting.empty()) ? 1 : 2;
    afh_ = choice("receiver.afh", {U("receiver.afh_auto"), U("receiver.afh_off"), U("receiver.afh_manual")}, afh_sel);
    afh_->setToolTip(U("receiver.afh_tooltip"));
    auto *afh_label = new QLabel(U("receiver.afh_channels"), this);
    afh_label->setProperty("secondary", true);
    sg->addWidget(afh_label, row, col++, Qt::AlignRight);
    afh_channels_ = new QLineEdit(afh_sel == 2 ? U8(afh_setting) : QString(), this);
    afh_channels_->setPlaceholderText("6,11");
    afh_channels_->setMaximumWidth(90);
    afh_channels_->setEnabled(afh_sel == 2);
    sg->addWidget(afh_channels_, row, col++);
    connect(afh_, &QComboBox::currentIndexChanged, this, [this] {
        afh_channels_->setEnabled(afh_->currentIndex() == 2);
        apply_afh();
    });
    connect(afh_channels_, &QLineEdit::textChanged, this, [this] { apply_afh(); });
    head("receiver.settings_received");
    mtu_ = choice("receiver.mtu", mtus, 0);
    mtu_->setToolTip(U("receiver.mtu_tooltip"));
    buffer_ = choice("receiver.buffer", buffers, 0);

    /* ---- Steps: OK / NG at a glance, then Start ---- */
    auto *start_row = new QHBoxLayout;
    auto *steps = new QGridLayout;
    steps->setHorizontalSpacing(18);
    for (int i = 0; i < STEP_COUNT; i++) {
        step_text_[i] = new QLabel(this);
        steps->addWidget(step_text_[i], i / 3, i % 3);
    }
    start_row->addLayout(steps);
    start_row->addStretch(1);
    start_btn_ = new QPushButton(U("receiver.start"), this);
    start_btn_->setProperty("accent", true);
    start_row->addWidget(start_btn_);
    root->addLayout(start_row);
    for (int i = 0; i < STEP_COUNT; i++) set_step(static_cast<Step>(i), StepState::Pending);

    /* ---- Comparison: sent | received ---- */
    QGridLayout *cg;
    card(&cg);
    cg->setColumnStretch(1, 1);
    cg->setColumnStretch(2, 1);
    for (int c = 0; c < 2; c++) {
        auto *h = heading(c == 0 ? "receiver.col_sent" : "receiver.col_received");
        h->setToolTip(U("receiver.help")); /* what the numbers mean, on hover */
        cg->addWidget(h, 0, c + 1);
    }
    auto add_row = [&](const char *label_key, QLabel **sent, QLabel **received) {
        const int r = cg->rowCount();
        auto *label = new QLabel(U(label_key), this);
        label->setProperty("secondary", true);
        cg->addWidget(label, r, 0);
        for (int c = 0; c < 2; c++) {
            auto *v = new QLabel("-", this);
            v->setMinimumWidth(200);
            v->setTextInteractionFlags(Qt::TextSelectableByMouse);
            cg->addWidget(v, r, c + 1);
            (c == 0 ? *sent : *received) = v;
        }
    };
    add_row("receiver.row_codec", &codec_s_, &codec_r_);
    add_row("receiver.row_bitrate", &bitrate_s_, &bitrate_r_);
    add_row("receiver.row_total", &total_s_, &total_r_);
    add_row("receiver.row_loss", &loss_s_, &loss_r_);
    add_row("receiver.row_timing", &timing_s_, &timing_r_);
    add_row("receiver.row_buffer", &buffer_s_, &buffer_r_);
    add_row("receiver.row_audio", &audio_s_, &audio_r_);
    add_row("receiver.row_errors", &errors_s_, &errors_r_);
    add_row("receiver.row_signal", &signal_s_, &signal_r_);

    /* ---- Log of what happened ---- */
    root->addWidget(heading("receiver.log"));
    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setFixedHeight(110);
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    log_->setFont(mono);
    root->addWidget(log_);

    auto *bar = new QHBoxLayout;
    auto *reset_btn = new QPushButton(U("quality.reset"), this);
    auto *copy_btn = new QPushButton(U("receiver.copy_log"), this);
    auto *close_btn = new QPushButton(U("modal.close"), this);
    for (QPushButton *b : {reset_btn, copy_btn, connect_btn_, start_btn_}) b->setAutoDefault(false);
    close_btn->setDefault(true);
    bar->addWidget(reset_btn);
    bar->addWidget(copy_btn);
    bar->addStretch(1);
    bar->addWidget(close_btn);
    root->addLayout(bar);

    connect(connect_btn_, &QPushButton::clicked, this, &ReceiverDialog::toggle_connection);
    connect(addr_, &QLineEdit::returnPressed, this, [this] {
        RemoteSinkClient::instance().stop();
        toggle_connection();
    });
    connect(start_btn_, &QPushButton::clicked, this, &ReceiverDialog::start_stop);
    connect(reset_btn, &QPushButton::clicked, this, &ReceiverDialog::reset);
    connect(copy_btn, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(log_->toPlainText()); });
    connect(close_btn, &QPushButton::clicked, this, &QDialog::close);
    connect(timer_, &QTimer::timeout, this, &ReceiverDialog::refresh);

    /* Find the receiver right away (address saved last time, else the local network) */
    if (RemoteSinkClient::instance().status() == RemoteSinkClient::Status::Off)
        RemoteSinkClient::instance().start(saved.empty() ? std::string(REMOTE_SINK_AUTO) : saved);

    last_state_ = backend_->state();
    baseline_ = take_sample();
    prev_ = baseline_;
    refresh();
    timer_->start(1000);
}

void ReceiverDialog::apply_afh() {
    std::string policy = afh_->currentIndex() == 0 ? "auto" : afh_->currentIndex() == 1 ? "off"
                       : afh_channels_->text().trimmed().toStdString();
    if (policy.empty()) policy = "off";
    BtStackTransport::set_afh_policy(policy);
    if (backend_->settings().afh != policy) {
        backend_->settings().afh = policy;
        backend_->settings().save();
    }
}

void ReceiverDialog::toggle_connection() {
    RemoteSinkClient &client = RemoteSinkClient::instance();
    if (client.status() != RemoteSinkClient::Status::Off) {
        client.stop();
    } else {
        std::string target = addr_->text().trimmed().toStdString();
        if (target.empty()) target = REMOTE_SINK_AUTO;
        client.start(target);
        have_remote_base_ = false;
        if (backend_->settings().remote_sink != target) {
            backend_->settings().remote_sink = target;
            backend_->settings().save();
        }
    }
    refresh();
}

void ReceiverDialog::start_stop() {
    if (measuring_) {
        log(U("receiver.log_stopped"));
        backend_->service().stop_streaming();
        measuring_ = false;
        refresh();
        return;
    }
    RemoteSinkInfo info;
    if (!RemoteSinkClient::instance().receiver(&info) || info.bt_address.empty()) return;
    ConnectionProfile p;
    p.name = L("receiver.profile_name");
    p.device_address = info.bt_address;
    p.device_name = info.bt_name;
    p.codec = ProfileManager::index_to_codec(codec_->currentIndex());
    p.quality = ProfileManager::index_to_quality(quality_->currentIndex());
    p.sample_rate = SAMPLE_RATES[rate_->currentIndex()];
    p.bit_depth = BIT_DEPTHS[depth_->currentIndex()];
    p.capture_mode = "loopback";
    p.test_tone = source_->currentIndex() == 0;
    /* Receiver side first: it applies the settings to the connection that follows */
    reset_measurement_steps();
    last_codec_.clear();
    requested_mtu_ = MTUS[mtu_->currentIndex()];
    requested_buffer_ms_ = BUFFERS_MS[buffer_->currentIndex()];
    RemoteSinkStats s;
    had_stats_at_start_ = RemoteSinkClient::instance().latest(&s);
    stream_id_at_start_ = had_stats_at_start_ && s.has_stream ? s.stream_id : 0;
    measure_start_ms_ = GetTickCount64();
    log(fmt("receiver.log_start", codec_->currentText(), quality_->currentText(), rate_->currentText(),
            depth_->currentText(), source_->currentText(), info.bt_address, mtu_->currentText(),
            buffer_->currentText()));
    set_step(StepConfigure, StepState::Running);
    RemoteSinkClient::instance().configure(requested_mtu_, requested_buffer_ms_);
    set_step(StepLink, StepState::Running);
    service_left_idle_ = false;
    backend_->start_stream(p);
    measuring_ = true;
    failure_.clear();
    refresh();
}

void ReceiverDialog::reset() {
    baseline_ = take_sample();
    have_remote_base_ = RemoteSinkClient::instance().latest(&remote_base_) && remote_base_.has_stream;
    refresh();
}

void ReceiverDialog::refresh() {
    const Sample now = take_sample();
    const A2dpService::State state = backend_->state();
    const bool streaming = state == A2dpService::State::Streaming;

    /* A new stream starts the counts from zero on both sides: here from the
     * counts when it started, as the receiver counts per stream, not from the
     * first refresh that sees it (up to a second of packets later) */
    const LinkStats &ls = link_stats();
    const uint32_t starts = ls.stream_starts.load();
    if (starts != stream_starts_) {
        stream_starts_ = starts;
        baseline_.packets = ls.start_packets.load();
        baseline_.bytes = ls.start_bytes.load();
        baseline_.dropped = ls.start_dropped.load();
        baseline_.capture_frames = ls.start_capture_frames.load();
        baseline_.capture_dropped = ls.start_capture_dropped.load();
    }
    if (streaming && last_state_ != A2dpService::State::Streaming) {
        prev_ = now;
        have_prev_ = false;
    }
    if (measuring_ && state != A2dpService::State::Idle) service_left_idle_ = true;
    if (measuring_ && (state == A2dpService::State::Error || (state == A2dpService::State::Idle && service_left_idle_))) {
        measuring_ = false;
        if (state == A2dpService::State::Error) failure_ = U8(backend_->status_text());
    }
    last_state_ = state;

    refresh_steps();
    refresh_status();
    refresh_sent(streaming, now);
    refresh_received();

    prev_ = now;
    have_prev_ = true;
}

void ReceiverDialog::log(const QString &line) {
    const QString text = QDateTime::currentDateTime().toString("HH:mm:ss ") + line;
    if (log_->toPlainText().size() > LOG_MAX_CHARS) {
        QString all = log_->toPlainText();
        log_->setPlainText(all.mid(all.size() / 2));
    }
    log_->appendPlainText(text);
}

void ReceiverDialog::set_step(Step step, StepState state, const QString &detail) {
    static const char16_t *MARKS[] = {u"○", u"…", u"✔", u"✖"}; /* ○ … ✔ ✖ */
    const bool changed = step_state_[step] != state;
    step_state_[step] = state;
    const int i = static_cast<int>(state);
    QLabel *l = step_text_[step];
    l->setText(QString::fromUtf16(MARKS[i]) + " " + U(STEP_KEYS[step]));
    l->setToolTip(detail);
    set_flag(l, "muted", state == StepState::Pending);
    set_flag(l, "warning", state == StepState::Running);
    set_flag(l, "ok", state == StepState::Ok);
    set_flag(l, "error", state == StepState::Failed);
    if (changed && log_ && (state == StepState::Ok || state == StepState::Failed))
        log(QString("%1 %2%3").arg(state == StepState::Ok ? "OK" : "NG", U(STEP_KEYS[step]),
                                   detail.isEmpty() ? QString() : ": " + detail));
}

void ReceiverDialog::reset_measurement_steps() {
    for (Step s : {StepConfigure, StepLink, StepCodec, StepAudio}) set_step(s, StepState::Pending);
    link_ok_ms_ = 0;
    last_service_text_.clear();
}

void ReceiverDialog::refresh_steps() {
    RemoteSinkClient &client = RemoteSinkClient::instance();
    std::string detail;
    const RemoteSinkClient::Status status = client.status(&detail);
    RemoteSinkInfo info;
    const bool have_info = client.receiver(&info);
    RemoteSinkStats s;
    const bool have_stats = client.latest(&s) && GetTickCount64() - s.received_tick < STATS_STALE_MS;
    const bool automatic = client.target() == REMOTE_SINK_AUTO;

    /* 1-2: finding the receiver and its statistics */
    switch (status) {
    case RemoteSinkClient::Status::Off:
        set_step(StepDiscover, StepState::Pending);
        set_step(StepStats, StepState::Pending);
        break;
    case RemoteSinkClient::Status::Connecting:
        if (!have_info) set_step(StepDiscover, automatic ? StepState::Running : StepState::Pending);
        set_step(StepStats, StepState::Running);
        break;
    case RemoteSinkClient::Status::Failed:
        if (!have_info && automatic) set_step(StepDiscover, StepState::Failed, U8(detail));
        else set_step(StepStats, StepState::Failed, U8(detail));
        break;
    case RemoteSinkClient::Status::Connected:
        if (have_info)
            set_step(StepDiscover, StepState::Ok,
                     QString("%1 (%2), Bluetooth %3").arg(U8(info.host), U8(info.ip), U8(info.bt_address)));
        if (have_stats) set_step(StepStats, StepState::Ok);
        break;
    }

    if (measure_start_ms_ == 0) return;
    const uint64_t now = GetTickCount64();

    /* 3: the receiver took the settings (it reports them in its statistics) */
    if (step_state_[StepConfigure] == StepState::Running) {
        const bool mtu_ok = requested_mtu_ == 0 || (have_stats && s.mtu == requested_mtu_);
        const bool buffer_ok = requested_buffer_ms_ == 0 || (have_stats && s.buffer_target_ms == requested_buffer_ms_);
        if (have_stats && mtu_ok && buffer_ok)
            set_step(StepConfigure, StepState::Ok, QString("MTU %1, %2 ms").arg(s.mtu).arg(s.buffer_target_ms));
        else if (now - measure_start_ms_ > CONFIGURE_TIMEOUT_MS)
            set_step(StepConfigure, StepState::Failed, U("receiver.log_configure_failed"));
    }

    /* 4: A2DPWB's own connection, with its status messages in the log */
    const A2dpService::State state = backend_->state();
    const std::string &service_text = backend_->status_text();
    if (measuring_ && service_text != last_service_text_) {
        last_service_text_ = service_text;
        log("A2DPWB: " + U8(service_text));
    }
    if (step_state_[StepLink] == StepState::Running) {
        if (state == A2dpService::State::Streaming) {
            set_step(StepLink, StepState::Ok);
            link_ok_ms_ = now;
            set_step(StepCodec, StepState::Running);
            set_step(StepAudio, StepState::Running);
        } else if (state == A2dpService::State::Error ||
                   (!measuring_ && service_left_idle_ && state == A2dpService::State::Idle)) {
            set_step(StepLink, StepState::Failed, U8(service_text));
        }
    } else if (step_state_[StepLink] == StepState::Ok && state == A2dpService::State::Reconnecting) {
        set_step(StepLink, StepState::Running);
        log(U("receiver.log_link_lost"));
    }

    /* 5-6: what the receiver sees of it */
    const bool new_stream = have_stats && s.has_stream && (s.stream_id != stream_id_at_start_ || !had_stats_at_start_);
    if (step_state_[StepCodec] == StepState::Running && new_stream) set_step(StepCodec, StepState::Ok, U8(s.config));
    if (step_state_[StepAudio] == StepState::Running) {
        if (new_stream && s.streaming && s.interval_packets > 0)
            set_step(StepAudio, StepState::Ok, fmt("receiver.bitrate_value", s.interval_bytes * 8.0 / 1000.0, (double)s.interval_packets));
        else if (link_ok_ms_ && now - link_ok_ms_ > AUDIO_TIMEOUT_MS)
            set_step(StepAudio, StepState::Failed, U("receiver.log_no_audio"));
    }
    if (step_state_[StepCodec] == StepState::Running && link_ok_ms_ && now - link_ok_ms_ > AUDIO_TIMEOUT_MS)
        set_step(StepCodec, StepState::Failed, U("receiver.log_no_audio"));
}

void ReceiverDialog::refresh_status() {
    RemoteSinkClient &client = RemoteSinkClient::instance();
    std::string detail;
    const RemoteSinkClient::Status status = client.status(&detail);
    const bool off = status == RemoteSinkClient::Status::Off;
    connect_btn_->setText(U(off ? "receiver.connect" : "receiver.disconnect"));
    addr_->setEnabled(off);

    RemoteSinkInfo info;
    const bool have_info = !off && client.receiver(&info);
    RemoteSinkStats s;
    const bool have_stats = client.latest(&s) && GetTickCount64() - s.received_tick < STATS_STALE_MS;

    QString text;
    bool bad = false;
    if (have_info)
        text = fmt("receiver.found", info.host, info.ip, info.bt_address, (unsigned)info.mtu) + "\n";
    switch (status) {
    case RemoteSinkClient::Status::Off:
        text += U("receiver.off");
        break;
    case RemoteSinkClient::Status::Connecting:
        text += U(client.target() == REMOTE_SINK_AUTO ? "receiver.searching" : "receiver.connecting");
        break;
    case RemoteSinkClient::Status::Failed:
        text += fmt("receiver.failed", detail);
        bad = true;
        break;
    case RemoteSinkClient::Status::Connected: {
        const A2dpService::State state = backend_->state();
        if (have_stats && s.streaming) text += U("receiver.state_receiving");
        else if (measuring_ && (state == A2dpService::State::Connecting || state == A2dpService::State::Reconnecting))
            text += U("receiver.state_connecting");
        else if (measuring_) text += U("receiver.state_waiting");
        else if (!failure_.isEmpty()) {
            text += fmt("receiver.state_failed", failure_);
            bad = true;
        } else text += U("receiver.state_ready");
        break;
    }
    }
    if (text != status_text_) {
        status_text_ = text;
        status_->setText(text);
    }
    set_flag(status_, "error", bad);

    start_btn_->setText(U(measuring_ ? "receiver.stop" : "receiver.start"));
    start_btn_->setEnabled(measuring_ || (have_info && !info.bt_address.empty()));
    for (QComboBox *c : {codec_, quality_, rate_, depth_, source_, mtu_, buffer_}) c->setEnabled(!measuring_);
}

void ReceiverDialog::refresh_sent(bool streaming, const Sample &now) {
    const A2dpService::StreamInfo &info = backend_->stream_info();
    if (!streaming || info.codec.empty()) {
        for (auto *t : {codec_s_, bitrate_s_, total_s_, loss_s_, timing_s_, buffer_s_, audio_s_, errors_s_, signal_s_})
            set_value(t, "-");
        if (last_codec_.isEmpty()) return;
        /* Stopped: the final counts (the counters are not reset, and nothing is sent any more) */
        set_value(codec_s_, fmt("receiver.final_value", last_codec_));
        const uint64_t sent = now.packets - baseline_.packets;
        const uint64_t dropped = now.dropped - baseline_.dropped;
        set_value(total_s_, QString::number(sent));
        set_value(loss_s_, fmt("receiver.dropped_value", (unsigned long long)dropped, pct(dropped, sent + dropped)), dropped > 0);
        return;
    }
    last_codec_ = QString("%1, %2 kHz").arg(U8(info.codec)).arg(info.sample_rate / 1000.0, 0, 'f', 1);
    set_value(codec_s_, last_codec_);
    const double dt = have_prev_ ? (now.tick_ms - prev_.tick_ms) / 1000.0 : 0.0;
    const uint64_t d_packets = now.packets - prev_.packets;
    if (dt > 0.2 && d_packets > 0)
        set_value(bitrate_s_, fmt("receiver.bitrate_value", (now.bytes - prev_.bytes) * 8.0 / dt / 1000.0, d_packets / dt));
    else
        set_value(bitrate_s_, "-");
    const uint64_t sent = now.packets - baseline_.packets;
    const uint64_t dropped = now.dropped - baseline_.dropped;
    set_value(total_s_, QString::number(sent));
    set_value(loss_s_, fmt("receiver.dropped_value", (unsigned long long)dropped, pct(dropped, sent + dropped)), dropped > 0);
    set_value(timing_s_, "-");
    set_value(buffer_s_, fmt("receiver.queue_value", (unsigned)link_stats().queue_depth.load(std::memory_order_relaxed),
                             (unsigned)MEDIA_QUEUE_PACKETS));
    const uint64_t cap_dropped = now.capture_dropped - baseline_.capture_dropped;
    const double cap_ms = info.source_sample_rate ? 1000.0 * cap_dropped / info.source_sample_rate : 0.0;
    set_value(audio_s_, fmt("receiver.capture_value", cap_ms), cap_dropped > 0);
    set_value(errors_s_, "-");
    set_value(signal_s_, radio_text(link_radio()));
}

void ReceiverDialog::refresh_received() {
    RemoteSinkStats s;
    const bool have = RemoteSinkClient::instance().latest(&s) && GetTickCount64() - s.received_tick < STATS_STALE_MS;
    if (!have || !s.has_stream) {
        for (auto *t : {codec_r_, bitrate_r_, total_r_, loss_r_, timing_r_, buffer_r_, audio_r_, errors_r_})
            set_value(t, "-");
        set_value(signal_r_, have && s.connected && s.has_rssi ? fmt("receiver.rssi_value", s.rssi) : QString("-"));
        return;
    }
    if (!have_remote_base_ || remote_base_.stream_id != s.stream_id) {
        remote_base_ = RemoteSinkStats{};
        remote_base_.stream_id = s.stream_id;
        have_remote_base_ = true;
    }
    const RemoteSinkStats &b = remote_base_;
    const uint64_t packets = s.packets - b.packets;
    const uint64_t lost = s.lost - b.lost;
    const uint64_t late = s.late - b.late;
    const uint64_t underruns = s.underruns - b.underruns;
    const uint64_t overflows = s.overflows - b.overflows;
    const uint64_t pauses = s.pauses - b.pauses;
    const uint64_t errors = (s.frame_errors - b.frame_errors) + (s.decode_errors - b.decode_errors) + (s.ts_errors - b.ts_errors);

    const QString config = U8(s.config);
    set_value(codec_r_, s.streaming ? config : fmt("receiver.final_value", config));
    if (s.streaming && s.interval_packets > 0) {
        set_value(bitrate_r_, fmt("receiver.bitrate_value", s.interval_bytes * 8.0 / 1000.0, (double)s.interval_packets));
        set_value(timing_r_, fmt("receiver.timing_value", s.jitter_ms, s.interval_max_gap_ms));
    } else {
        set_value(bitrate_r_, "-");
        set_value(timing_r_, "-");
    }
    set_value(total_r_, QString::number(packets));
    set_value(loss_r_, fmt("receiver.lost_value", (unsigned long long)lost, pct(lost, packets + lost), (unsigned long long)late),
              lost || late);
    set_value(buffer_r_, s.buffer_ms < 0 ? QString("-") : fmt("receiver.buffer_value", s.buffer_ms, (unsigned)s.buffer_target_ms));
    QString audio = fmt("receiver.dropouts_value", (unsigned long long)underruns, s.underrun_ms - b.underrun_ms,
                        (unsigned long long)overflows);
    if (pauses) audio += fmt("receiver.pauses_value", (unsigned long long)pauses);
    if (s.idle) audio = U("receiver.idle_value") + audio;
    set_value(audio_r_, audio, underruns || overflows);
    set_value(errors_r_, fmt(s.decoding ? "receiver.errors_value" : "receiver.errors_value_headers", (unsigned long long)errors),
              errors > 0);
    set_value(signal_r_, s.has_rssi || s.afh_channels >= 0
                             ? radio_text(s.has_rssi, s.rssi, s.afh_channels, std::string())
                             : U8(s.rssi_note.empty() ? "-" : s.rssi_note));
}
