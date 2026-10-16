/*
 * Debug console implementation
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "debug_console_dialog.h"

#include "hci_capture.h"
#include "link_stats.h"
#include "main_window.h"
#include "theme.h"
#include "ui_util.h"

#include <QAbstractTableModel>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSysInfo>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include <windows.h>

#ifndef APP_VERSION
#define APP_VERSION "1.0.1"
#endif

namespace {

const char *DIALOG_NAME = "a2dpwb_debug_console";
const int REFRESH_MS = 500;
/* Diagnostics: how much of the current connection attempt's log to include */
const size_t DIAG_ISSUES = 40;
const size_t DIAG_LINES = 200;

enum FlowColumn { FCOL_STEP, FCOL_STATUS, FCOL_TIME, FCOL_ELAPSED, FCOL_DETAIL, FCOL_COUNT };

using Level = DebugLogModel::Level;
using StepStatus = DebugLogModel::StepStatus;

/* Level choice index -> lowest level shown */
const Level CHOICE_LEVELS[] = {Level::Debug, Level::Info, Level::Warn, Level::Error};

const char *status_key(StepStatus s) {
    switch (s) {
    case StepStatus::Pending: return "dbgc.status.pending";
    case StepStatus::Running: return "dbgc.status.running";
    case StepStatus::Done:    return "dbgc.status.done";
    case StepStatus::Warning: return "dbgc.status.warning";
    case StepStatus::Failed:  return "dbgc.status.failed";
    }
    return "";
}

/* Plain-text marker for copied flows (no localization, fixed width) */
const char *status_tag(StepStatus s) {
    switch (s) {
    case StepStatus::Pending: return "[ -- ]";
    case StepStatus::Running: return "[ .. ]";
    case StepStatus::Done:    return "[ OK ]";
    case StepStatus::Warning: return "[WARN]";
    case StepStatus::Failed:  return "[FAIL]";
    }
    return "";
}

QColor status_colour(StepStatus s) {
    switch (s) {
    case StepStatus::Pending: return TH().color(Role::TextMuted);
    case StepStatus::Running: return TH().color(Role::StatusConnecting);
    case StepStatus::Done:    return TH().color(Role::StatusStreaming);
    case StepStatus::Warning: return TH().color(Role::StatusReconnecting);
    case StepStatus::Failed:  return TH().color(Role::StatusError);
    }
    return TH().color(Role::Text);
}

/* "hh:mm:ss.mmm" -> ms since midnight, -1 if not a time */
long parse_ms(const QString &t) {
    unsigned h, m, s, ms;
    if (t.length() != 12 || sscanf(t.toUtf8().constData(), "%u:%u:%u.%u", &h, &m, &s, &ms) != 4) return -1;
    return static_cast<long>(((h * 60 + m) * 60 + s) * 1000 + ms);
}

QString elapsed_text(const QString &from, const QString &to) {
    const long a = parse_ms(from), b = parse_ms(to);
    if (a < 0 || b < 0) return {};
    const long day = 24L * 3600 * 1000;
    long d = b - a;
    if (d < -day / 2) d += day; /* across midnight */
    /* Plain stderr lines are stamped when read (up to one poll late), so a LOG_*
     * line can look slightly older than the line before it */
    if (d < 0 && d > -1000) d = 0;
    if (d < 0) return {};
    return QString("+%1 s").arg(d / 1000.0, 0, 'f', 3);
}

const char *state_name(int s) {
    switch (s) {
    case 0: return "Idle";
    case 1: return "Connecting";
    case 2: return "Streaming";
    case 3: return "Reconnecting";
    case 4: return "Error";
    }
    return "?";
}

/* "AA:BB:CC:DD:EE:FF" -> "AA:BB:CC:xx:xx:xx": keeps the vendor part (OUI) */
void mask_bd_addresses(QString &text) {
    const int len = 17;
    auto part = [&](int j) { return text[j] == ':' || text[j].isDigit() || (text[j].toLower() >= 'a' && text[j].toLower() <= 'f'); };
    auto is_addr_at = [&](int i) {
        if (i + len > text.size()) return false;
        for (int k = 0; k < len; k++) {
            const QChar c = text[i + k];
            if (k % 3 == 2) {
                if (c != ':') return false;
            } else if (!(c.isDigit() || (c.toLower() >= 'a' && c.toLower() <= 'f'))) {
                return false;
            }
        }
        /* Not part of a longer hex / colon run */
        if (i > 0 && part(i - 1)) return false;
        if (i + len < text.size() && part(i + len)) return false;
        return true;
    };
    for (int i = 0; i + len <= text.size(); i++) {
        if (!is_addr_at(i)) continue;
        text.replace(i + 9, 8, "xx:xx:xx");
        i += len - 1;
    }
}

} // namespace

/* ======================================================================== */
/* Virtual log table                                                         */
/* ======================================================================== */

class LogTableModel : public QAbstractTableModel {
public:
    explicit LogTableModel(DebugConsoleDialog *dlg) : QAbstractTableModel(dlg), dlg_(dlg) {}

    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(dlg_->shown_.size());
    }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 3; }

    QVariant data(const QModelIndex &idx, int role) const override {
        if (!idx.isValid() || idx.row() >= static_cast<int>(dlg_->shown_.size())) return {};
        const DebugLogModel::Entry *e = dlg_->model_->find(dlg_->shown_[idx.row()]);
        if (!e) return {};
        if (role == Qt::DisplayRole) {
            switch (idx.column()) {
            case 0: return e->time;
            case 1: return QString::fromLatin1(DebugLogModel::level_name(e->level));
            case 2: return e->text;
            }
        } else if (role == Qt::ForegroundRole) {
            switch (e->level) {
            case Level::Error: return TH().color(Role::StatusError);
            case Level::Warn:  return TH().color(Role::Warning);
            case Level::Debug: return TH().color(Role::TextMuted);
            case Level::Info:  break;
            }
        }
        return {};
    }

    QVariant headerData(int section, Qt::Orientation o, int role) const override {
        if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
        static const char *keys[] = {"dbgc.col_time", "dbgc.col_level", "dbgc.col_message"};
        return U(keys[section]);
    }

    /* The shown list was rebuilt */
    void reset() {
        beginResetModel();
        endResetModel();
    }
    /* Rows [first, last] were appended to the shown list */
    void appended(int first, int last) {
        beginInsertRows({}, first, last);
        endInsertRows();
    }

private:
    DebugConsoleDialog *dlg_;
};

/* ======================================================================== */
/* Construction                                                              */
/* ======================================================================== */

void DebugConsoleDialog::show_for(MainWindow *window, DebugLogModel *model) {
    if (!model) return;
    if (auto *existing = window->findChild<DebugConsoleDialog *>(DIALOG_NAME)) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto *dlg = new DebugConsoleDialog(window, model);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

DebugConsoleDialog::DebugConsoleDialog(MainWindow *window, DebugLogModel *model)
    : QDialog(window), backend_(&window->backend()), model_(model), timer_(new QTimer(this)) {
    setObjectName(DIALOG_NAME);
    setWindowTitle(U("dbgc.title"));
    setWindowFlag(Qt::WindowMaximizeButtonHint);
    setWindowFlag(Qt::WindowMinimizeButtonHint);
    resize(1000, 740);
    setMinimumSize(760, 560);

    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(9);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(8);

    auto heading = [&](const char *key) {
        auto *h = new QLabel(U(key), this);
        h->setProperty("heading", true);
        return h;
    };

    /* ---- Connection flow ---- */
    auto *flow_head = new QHBoxLayout;
    flow_head->addWidget(heading("dbgc.section_flow"));
    session_label_ = new QLabel(this);
    session_label_->setProperty("secondary", true);
    flow_head->addWidget(session_label_, 1);
    root->addLayout(flow_head);

    flow_list_ = new QTableWidget(DebugLogModel::STEP_COUNT, FCOL_COUNT, this);
    flow_list_->setHorizontalHeaderLabels({U("dbgc.col_step"), U("dbgc.col_status"), U("dbgc.col_time"),
                                           U("dbgc.col_elapsed"), U("dbgc.col_detail")});
    flow_list_->verticalHeader()->hide();
    flow_list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    flow_list_->setSelectionBehavior(QAbstractItemView::SelectRows);
    flow_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    flow_list_->setShowGrid(false);
    flow_list_->horizontalHeader()->setStretchLastSection(true);
    flow_list_->setColumnWidth(FCOL_STEP, 230);
    flow_list_->setColumnWidth(FCOL_STATUS, 90);
    flow_list_->setColumnWidth(FCOL_TIME, 100);
    flow_list_->setColumnWidth(FCOL_ELAPSED, 90);
    for (int s = 0; s < DebugLogModel::STEP_COUNT; s++) {
        flow_list_->setItem(s, FCOL_STEP, new QTableWidgetItem(U(DebugLogModel::step_label_key(DebugLogModel::Step(s)))));
        for (int c = 1; c < FCOL_COUNT; c++) flow_list_->setItem(s, c, new QTableWidgetItem);
    }
    flow_list_->resizeRowsToContents();
    const int row_h = flow_list_->rowHeight(0);
    flow_list_->setFixedHeight(row_h * (DebugLogModel::STEP_COUNT + 1) + 8);
    root->addWidget(flow_list_);
    auto *flow_hint = new QLabel(U("dbgc.flow_hint"), this);
    flow_hint->setProperty("muted", true);
    root->addWidget(flow_hint);

    /* ---- Log ---- */
    auto *log_head = new QHBoxLayout;
    log_head->addWidget(heading("dbgc.section_log"));
    level_choice_ = new QComboBox(this);
    for (const char *k : {"dbgc.level_all", "dbgc.level_info", "dbgc.level_warn", "dbgc.level_error"})
        level_choice_->addItem(U(k));
    log_head->addWidget(level_choice_);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(U("dbgc.search_hint"));
    search_->setClearButtonEnabled(true);
    search_->setMinimumWidth(200);
    log_head->addWidget(search_);
    session_only_ = new QCheckBox(U("dbgc.session_only"), this);
    log_head->addWidget(session_only_);
    autoscroll_ = new QCheckBox(U("dbgc.autoscroll"), this);
    autoscroll_->setChecked(true);
    log_head->addWidget(autoscroll_);
    log_head->addStretch(1);
    count_label_ = new QLabel(this);
    count_label_->setProperty("secondary", true);
    log_head->addWidget(count_label_);
    root->addLayout(log_head);

    log_model_ = new LogTableModel(this);
    log_view_ = new QTableView(this);
    log_view_->setModel(log_model_);
    log_view_->setFont(mono);
    log_view_->verticalHeader()->hide();
    log_view_->verticalHeader()->setDefaultSectionSize(QFontMetrics(mono).height() + 6);
    log_view_->setShowGrid(false);
    log_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    log_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    log_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    log_view_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    log_view_->horizontalHeader()->setStretchLastSection(true);
    log_view_->setColumnWidth(0, 105);
    log_view_->setColumnWidth(1, 62);
    log_view_->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *copy_sc = new QShortcut(QKeySequence::Copy, log_view_);
    copy_sc->setContext(Qt::WidgetShortcut);
    connect(copy_sc, &QShortcut::activated, this, &DebugConsoleDialog::copy_selected_lines);
    root->addWidget(log_view_, 1);

    /* ---- Buttons ---- */
    auto *bar = new QHBoxLayout;
    auto *diag_btn = new QPushButton(U("dbgc.copy_diag"), this);
    auto *flow_btn = new QPushButton(U("dbgc.copy_flow"), this);
    auto *log_btn = new QPushButton(U("dbgc.copy_log"), this);
    bar->addWidget(diag_btn);
    bar->addWidget(flow_btn);
    bar->addWidget(log_btn);
    mask_addr_ = new QCheckBox(U("dbgc.mask"), this);
    mask_addr_->setChecked(true);
    mask_addr_->setToolTip(U("dbgc.mask_tip"));
    bar->addWidget(mask_addr_);
    feedback_label_ = new QLabel(this);
    feedback_label_->setProperty("ok", true);
    bar->addWidget(feedback_label_, 1);
    auto *open_btn = new QPushButton(U("debug.open_log"), this);
    bar->addWidget(open_btn);
    auto *close_btn = new QPushButton(U("modal.close"), this);
    bar->addWidget(close_btn);
    root->addLayout(bar);

    /* ---- Events ---- */
    connect(diag_btn, &QPushButton::clicked, this, [this] {
        const QString text = diagnostics_text();
        copy_to_clipboard(text, text.count('\n'));
    });
    connect(flow_btn, &QPushButton::clicked, this, [this] {
        const QString text = flow_text();
        copy_to_clipboard(text, text.count('\n'));
    });
    connect(log_btn, &QPushButton::clicked, this, &DebugConsoleDialog::copy_shown_lines);
    connect(open_btn, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(U8(model_->path()))); });
    connect(close_btn, &QPushButton::clicked, this, &QDialog::close);

    connect(level_choice_, &QComboBox::currentIndexChanged, this, [this] { rebuild_filter(); });
    connect(session_only_, &QCheckBox::toggled, this, [this] { rebuild_filter(); });
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuild_filter(); });
    connect(log_view_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        menu.addAction(U("dbgc.copy_selected"), this, &DebugConsoleDialog::copy_selected_lines);
        menu.addAction(U("dbgc.copy_log"), this, &DebugConsoleDialog::copy_shown_lines);
        menu.exec(log_view_->viewport()->mapToGlobal(pos));
    });
    /* Selecting a line above the end stops following new lines */
    connect(log_view_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &cur, const QModelIndex &) {
                if (cur.isValid() && cur.row() + 1 < static_cast<int>(shown_.size())) autoscroll_->setChecked(false);
            });
    connect(flow_list_, &QTableWidget::cellDoubleClicked, this, [this](int step, int) {
        if (step >= 0 && step < DebugLogModel::STEP_COUNT) jump_to(model_->session().steps[step].seq);
    });
    connect(timer_, &QTimer::timeout, this, &DebugConsoleDialog::on_timer);

    prev_tick_ = GetTickCount64();
    prev_packets_ = link_stats().packets_sent.load(std::memory_order_relaxed);
    prev_bytes_ = link_stats().bytes_sent.load(std::memory_order_relaxed);
    prev_capture_ = link_stats().capture_frames.load(std::memory_order_relaxed);

    model_->poll();
    rebuild_filter();
    refresh_flow();
    timer_->start(REFRESH_MS);
}

/* ======================================================================== */
/* Log view                                                                  */
/* ======================================================================== */

bool DebugConsoleDialog::matches(const DebugLogModel::Entry &e) const {
    const int sel = level_choice_->currentIndex();
    if (sel > 0 && e.level < CHOICE_LEVELS[sel]) return false;
    if (session_only_->isChecked() && e.session != model_->session().id) return false;
    if (!query_lower_.isEmpty() && !e.text.toLower().contains(query_lower_)) return false;
    return true;
}

void DebugConsoleDialog::rebuild_filter() {
    query_lower_ = search_->text().toLower();
    shown_.clear();
    for (const auto &e : model_->entries())
        if (matches(e)) shown_.push_back(e.seq);
    seen_seq_ = model_->last_seq();
    seen_first_ = model_->first_seq();
    seen_session_ = model_->session().id;

    log_model_->reset();
    if (autoscroll_->isChecked() && !shown_.empty()) log_view_->scrollToBottom();
    update_count();
}

void DebugConsoleDialog::append_new() {
    /* Old lines trimmed, or a new attempt while only the current one is shown */
    if (model_->first_seq() != seen_first_ ||
        (session_only_->isChecked() && model_->session().id != seen_session_)) {
        rebuild_filter();
        return;
    }
    const uint64_t last = model_->last_seq();
    if (last == seen_seq_) return;
    const size_t before = shown_.size();
    for (uint64_t seq = seen_seq_ + 1; seq <= last; seq++) {
        const DebugLogModel::Entry *e = model_->find(seq);
        if (e && matches(*e)) shown_.push_back(seq);
    }
    seen_seq_ = last;
    seen_session_ = model_->session().id;
    if (shown_.size() != before) {
        log_model_->appended(static_cast<int>(before), static_cast<int>(shown_.size()) - 1);
        if (autoscroll_->isChecked()) log_view_->scrollToBottom();
    }
    update_count();
}

void DebugConsoleDialog::update_count() {
    count_label_->setText(fmt("dbgc.count", (unsigned)shown_.size(), (unsigned)model_->entries().size()));
}

void DebugConsoleDialog::jump_to(uint64_t seq) {
    if (seq == 0 || !model_->find(seq)) return;
    auto it = std::lower_bound(shown_.begin(), shown_.end(), seq);
    if (it == shown_.end() || *it != seq) {
        /* Hidden by the filters: show everything */
        QSignalBlocker b1(level_choice_), b2(search_);
        level_choice_->setCurrentIndex(0);
        search_->clear();
        rebuild_filter();
        it = std::lower_bound(shown_.begin(), shown_.end(), seq);
        if (it == shown_.end() || *it != seq) return;
    }
    autoscroll_->setChecked(false);
    const int row = static_cast<int>(it - shown_.begin());
    log_view_->selectRow(row);
    log_view_->scrollTo(log_model_->index(row, 0), QAbstractItemView::PositionAtCenter);
    log_view_->setFocus();
}

/* ======================================================================== */
/* Flow                                                                      */
/* ======================================================================== */

DebugConsoleDialog::FlowRow DebugConsoleDialog::flow_row(int step, bool english) const {
    /* Copied text is English (bug reports); the window follows the UI language */
    auto T = [english](const char *key, const char *en) { return english ? QString(en) : U(key); };
    const DebugLogModel::Session &sess = model_->session();
    const DebugLogModel::FlowStep &st = sess.steps[step];
    FlowRow r{st.status, st.time, QString(), st.detail};
    if (!st.time.isEmpty()) r.elapsed = elapsed_text(sess.start_time, st.time);

    const bool active = sess.id != 0 && sess.stop_time.isEmpty();
    const A2dpService::State state = backend_->state();

    /* The step being worked on: first unfinished one while connecting */
    if (active && r.status == StepStatus::Pending && state == A2dpService::State::Connecting) {
        bool first_open = true;
        for (int s = 0; s < step; s++) {
            const StepStatus p = sess.steps[s].status;
            if (p != StepStatus::Done && p != StepStatus::Warning) { first_open = false; break; }
        }
        if (first_open) r.status = StepStatus::Running;
    }

    if (step == DebugLogModel::STEP_AUDIO && active &&
        (r.status == StepStatus::Done || r.status == StepStatus::Warning) &&
        state == A2dpService::State::Streaming && pps_ >= 0) {
        if (pps_ < 1.0 && capture_fps_ < 1.0) {
            /* Loopback capture gets no data while nothing plays, so nothing is
             * sent either: normal while playback is paused */
            r.status = StepStatus::Done;
            r.detail = T("dbgc.audio_idle", "No audio playing - nothing to send");
        } else if (pps_ < 1.0) {
            r.status = StepStatus::Warning;
            r.detail = T("dbgc.audio_stalled", "Audio captured but no packets sent in the last second");
        } else {
            r.detail = QString::asprintf(T("dbgc.audio_live", "%.0f packets/s, %.0f kbps").toUtf8().constData(), pps_, kbps_);
        }
    }
    if (step == DebugLogModel::STEP_LINK && sess.id != 0) {
        if (sess.reconnects > 0) {
            const QString count = QString::asprintf(T("dbgc.link_reconnects", "%d reconnect(s)").toUtf8().constData(),
                                                    sess.reconnects);
            r.detail = r.detail.isEmpty() ? count : count + " - " + r.detail;
        } else if (r.status == StepStatus::Done) {
            r.detail = T("dbgc.link_stable", "No disconnects");
        }
    }
    return r;
}

void DebugConsoleDialog::refresh_flow() {
    const DebugLogModel::Session &sess = model_->session();

    QString label;
    if (sess.id == 0) {
        label = U("dbgc.no_session");
    } else {
        label = fmt("dbgc.session", (int)sess.id, sess.start_time);
        if (!sess.stop_time.isEmpty()) label += fmt("dbgc.session_stopped", sess.stop_time);
    }
    if (!model_->last_state_text().isEmpty()) label += "   |   " + model_->last_state_text();
    if (session_label_->text() != label) session_label_->setText(label);

    for (int s = 0; s < DebugLogModel::STEP_COUNT; s++) {
        const FlowRow r = flow_row(s);
        const QString cells[] = {QString(), U(status_key(r.status)), r.time, r.elapsed, r.detail};
        for (int c = 1; c < FCOL_COUNT; c++)
            if (flow_list_->item(s, c)->text() != cells[c]) flow_list_->item(s, c)->setText(cells[c]);
        const QColor colour = status_colour(r.status);
        for (int c = 0; c < FCOL_COUNT; c++) flow_list_->item(s, c)->setForeground(colour);
    }
}

void DebugConsoleDialog::on_timer() {
    /* Packet rate over the last period */
    const uint64_t tick = GetTickCount64();
    const uint64_t packets = link_stats().packets_sent.load(std::memory_order_relaxed);
    const uint64_t bytes = link_stats().bytes_sent.load(std::memory_order_relaxed);
    const uint64_t capture = link_stats().capture_frames.load(std::memory_order_relaxed);
    const double dt = (tick - prev_tick_) / 1000.0;
    if (dt >= 0.9) {
        pps_ = (packets - prev_packets_) / dt;
        kbps_ = (bytes - prev_bytes_) * 8.0 / dt / 1000.0;
        capture_fps_ = (capture - prev_capture_) / dt;
        prev_tick_ = tick;
        prev_packets_ = packets;
        prev_bytes_ = bytes;
        prev_capture_ = capture;
    }

    model_->poll();
    append_new();
    refresh_flow();
}

/* ======================================================================== */
/* Copies                                                                    */
/* ======================================================================== */

QString DebugConsoleDialog::format_line(const DebugLogModel::Entry &e) const {
    return e.time.leftJustified(12) + " " + QString::fromLatin1(DebugLogModel::level_name(e.level)).leftJustified(5) +
           " " + e.text;
}

QString DebugConsoleDialog::flow_text() const {
    const DebugLogModel::Session &sess = model_->session();
    QString out;
    if (sess.id == 0) return "Connection flow: no connection attempt yet\n";
    out += QString("Connection flow: attempt #%1, started %2").arg(sess.id).arg(sess.start_time);
    if (!sess.stop_time.isEmpty()) out += ", stopped " + sess.stop_time;
    if (sess.reconnects > 0) out += QString(", %1 reconnect(s)").arg(sess.reconnects);
    out += "\n";
    for (int s = 0; s < DebugLogModel::STEP_COUNT; s++) {
        const FlowRow r = flow_row(s, true);
        out += QString("  %1 %2 %3 %4 %5\n")
                   .arg(status_tag(r.status), QString::fromLatin1(DebugLogModel::step_name(DebugLogModel::Step(s))).leftJustified(32),
                        r.time.leftJustified(12), r.elapsed.leftJustified(10), r.detail);
    }
    return out;
}

QString DebugConsoleDialog::diagnostics_text() const {
    const AppSettings &set = backend_->settings();
    QString out;
    out += "```text\n";
    out += "A2DPWB diagnostics - " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") + "\n";
    out += QString("Version      : %1\n").arg(APP_VERSION);
    out += "OS           : " + QSysInfo::prettyProductName() + "\n";
    out += "Language     : " + U8(set.language) + ", theme " + U8(set.theme) + "\n";
    out += QString("State        : %1").arg(state_name(static_cast<int>(backend_->state())));
    if (!model_->last_state_text().isEmpty()) out += " (" + model_->last_state_text() + ")";
    out += "\n";

    out += "\n[Adapter]\n";
    const char *chip_kind = "";
    if (BtAdapterEnumerator::is_auto_chip(set.bt_chip_pid, set.bt_chip_fw_stem)) chip_kind = " (auto)";
    else if (set.bt_chip_pid == NON_REALTEK_CHIP_PID) chip_kind = " (not Realtek)";
    else if (set.bt_chip_pid == CUSTOM_CHIP_PID) chip_kind = " (custom)";
    out += fmt_lit("  Setting: chip PID 0x%04X%s", (unsigned)set.bt_chip_pid, chip_kind) +
           (set.bt_chip_fw_stem.empty() ? QString() : ", firmware stem " + U8(set.bt_chip_fw_stem)) + "\n";
    A2dpService &svc = backend_->service();
    out += QString("  Firmware files: ") + (svc.is_firmware_present() ? "present" : "missing");
    const std::string fw = svc.firmware_status();
    if (!fw.empty()) out += " (" + U8(fw) + ")";
    out += "\n";
    for (const QString &l : model_->adapter_lines()) out += "  " + l + "\n";

    out += "\n[Profile]\n";
    const int idx = backend_->selected_profile();
    const auto &profs = backend_->profiles().profiles();
    if (idx >= 0 && idx < static_cast<int>(profs.size())) {
        const ConnectionProfile &p = profs[idx];
        out += "  Name: " + U8(p.name) + "\n";
        out += "  Device: " + U8(p.device_name) + " [" + U8(p.device_address) + "]\n";
        out += QString("  Codec: %1, quality %2, ABR %3, sample rate %4, bit depth %5\n")
                   .arg(U8(p.codec), U8(p.quality), p.abr ? "on" : "off",
                        p.sample_rate ? QString::number(p.sample_rate) : QString("auto"),
                        p.bit_depth ? QString::number(p.bit_depth) : QString("auto"));
        out += "  Capture: " + U8(p.capture_mode);
        if (p.capture_mode == "app") {
            if (!p.app_exe.empty()) out += " '" + U8(p.app_name.empty() ? p.app_exe : p.app_name) + "'";
        } else if (!p.audio_device_name.empty()) {
            out += " '" + U8(p.audio_device_name) + "'";
        }
        out += QString(", auto switch %1, max media packet %2\n").arg(p.auto_switch_device ? "on" : "off").arg(p.max_media_payload);
        out += "  Volume sync: " + U8(p.volume_sync);
        if (p.volume_sync == "device") out += " '" + U8(p.volume_device_name) + "'";
        out += "\n";
    } else {
        out += "  (none selected - receiver measurement or not connected)\n";
    }

    out += "\n[Stream]\n";
    const A2dpService::StreamInfo &info = backend_->stream_info();
    if (backend_->state() == A2dpService::State::Streaming && !info.codec.empty()) {
        out += QString("  %1 %2 Hz %3 ch, %4 kbps; source %5 Hz %6 ch %7 bit\n")
                   .arg(U8(info.codec)).arg(info.sample_rate).arg(info.channels).arg(info.bitrate_kbps)
                   .arg(info.source_sample_rate).arg(info.source_channels).arg(info.source_bit_depth);
    } else {
        out += "  not streaming\n";
    }
    const LinkStats &ls = link_stats();
    out += QString("  Media packets sent %1 (%2 bytes), queue drops %3, send errors %4\n")
               .arg(ls.packets_sent.load()).arg(ls.bytes_sent.load()).arg(ls.queue_drops.load()).arg(ls.send_errors.load());
    out += QString("  Capture frames %1, dropped %2\n").arg(ls.capture_frames.load()).arg(ls.capture_dropped_frames.load());
    if (hci_capture::active())
        out += QString("  HCI capture running: %1 (%2 MB)\n")
                   .arg(U8(hci_capture::path())).arg(hci_capture::bytes_written() / (1024.0 * 1024.0), 0, 'f', 1);

    out += "\n[Flow]\n" + flow_text();

    /* Lines of the current attempt (everything if none yet) */
    const uint32_t sid = model_->session().id;
    std::vector<const DebugLogModel::Entry *> lines, issues;
    for (const auto &e : model_->entries()) {
        if (e.session != sid) continue;
        lines.push_back(&e);
        if (e.level >= Level::Warn) issues.push_back(&e);
    }

    out += "\n[Warnings and errors]\n";
    if (issues.empty()) out += "  none\n";
    size_t skip = issues.size() > DIAG_ISSUES ? issues.size() - DIAG_ISSUES : 0;
    if (skip) out += QString("  (%1 earlier omitted)\n").arg(skip);
    for (size_t i = skip; i < issues.size(); i++) out += "  " + format_line(*issues[i]) + "\n";

    out += "\n[Log]\n";
    skip = lines.size() > DIAG_LINES ? lines.size() - DIAG_LINES : 0;
    if (skip) out += QString("(%1 earlier lines omitted - see debug.log)\n").arg(skip);
    for (size_t i = skip; i < lines.size(); i++) out += format_line(*lines[i]) + "\n";
    out += "```\n";
    return out;
}

void DebugConsoleDialog::copy_selected_lines() {
    QString text;
    int n = 0;
    QModelIndexList rows = log_view_->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(), [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
    for (const QModelIndex &i : rows) {
        if (i.row() >= static_cast<int>(shown_.size())) break;
        if (const DebugLogModel::Entry *e = model_->find(shown_[i.row()])) {
            text += format_line(*e) + "\n";
            n++;
        }
    }
    if (n) copy_to_clipboard(text, n);
}

void DebugConsoleDialog::copy_shown_lines() {
    QString text;
    int n = 0;
    for (uint64_t seq : shown_) {
        if (const DebugLogModel::Entry *e = model_->find(seq)) {
            text += format_line(*e) + "\n";
            n++;
        }
    }
    copy_to_clipboard(text, n);
}

void DebugConsoleDialog::copy_to_clipboard(const QString &text_in, int lines) {
    QString text = text_in;
    if (mask_addr_->isChecked()) mask_bd_addresses(text);
    QApplication::clipboard()->setText(text);
    feedback_label_->setText(fmt("dbgc.copied", (unsigned)lines));
}
