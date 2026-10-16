/*
 * Debug Log Model Implementation
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "debug_log_model.h"

#include <QTime>

#include <algorithm>
#include <cstring>
#include <QTimer>

#include <windows.h>

namespace {

/* Lines kept in memory (debug.log itself keeps everything) */
const size_t MAX_ENTRIES = 100000;
/* On the first read of a large file, start this far from its end */
const uint64_t MAX_INITIAL_BYTES = 16ull * 1024 * 1024;
/* Plain stderr lines get the time they are read: poll often */
const int POLL_MS = 100;

enum class Action { Done, Running, Warning, Failed, Detail };
enum class DetailFrom { None, After, Whole };

struct Rule {
    DebugLogModel::Step step;
    const char *needle;
    Action action;
    DetailFrom detail;
};

/* Milestones, recognised from the lines the modules already print. Keep in
 * sync with the messages in a2dp_service.cpp / btstack_transport.cpp /
 * wasapi_capture.cpp. Failures that only show up as an error state are
 * caught by the "state=4" handling in track(). */
const Rule RULES[] = {
    {DebugLogModel::STEP_START,   "A2dpService:   profile=",                       Action::Detail,  DetailFrom::After},
    {DebugLogModel::STEP_BTSTACK, "BTstack: thread started",                       Action::Running, DetailFrom::None},
    {DebugLogModel::STEP_BTSTACK, "BTstack: Controller ",                          Action::Detail,  DetailFrom::After},
    {DebugLogModel::STEP_BTSTACK, "BTstack: WARNING",                              Action::Warning, DetailFrom::Whole},
    {DebugLogModel::STEP_BTSTACK, "BTstack: HCI initialization failed",            Action::Failed,  DetailFrom::Whole},
    {DebugLogModel::STEP_BTSTACK, "BTstack: HCI initialization timed out",         Action::Failed,  DetailFrom::Whole},
    {DebugLogModel::STEP_BTSTACK, "A2dpService: BTstack initialized successfully", Action::Done,    DetailFrom::None},
    {DebugLogModel::STEP_CONNECT, "BTstack: Connecting to ",                       Action::Running, DetailFrom::After},
    {DebugLogModel::STEP_CONNECT, "BTstack: HCI ACL connection established",       Action::Detail,  DetailFrom::Whole},
    {DebugLogModel::STEP_CONNECT, "BTstack: Signaling connection established",     Action::Done,    DetailFrom::Whole},
    {DebugLogModel::STEP_CONNECT, "BTstack: Connection timed out",                 Action::Failed,  DetailFrom::Whole},
    {DebugLogModel::STEP_CONNECT, "a2dp_source_establish_stream failed",           Action::Failed,  DetailFrom::Whole},
    {DebugLogModel::STEP_CAPS,    "BTstack: Remote SEPs in use by another connection", Action::Warning, DetailFrom::Whole},
    {DebugLogModel::STEP_CAPS,    "BTstack: Capability discovery complete",        Action::Done,    DetailFrom::After},
    {DebugLogModel::STEP_CODEC,   "A2dpService: codec fallback: ",                 Action::Warning, DetailFrom::After},
    {DebugLogModel::STEP_CODEC,   "A2dpService: codec selected: ",                 Action::Done,    DetailFrom::After},
    {DebugLogModel::STEP_CAPTURE, "WasapiCapture: Format: ",                       Action::Detail,  DetailFrom::After},
    {DebugLogModel::STEP_CAPTURE, "A2dpService: WASAPI capture init OK",           Action::Done,    DetailFrom::After},
    {DebugLogModel::STEP_CONFIG,  "BTstack: Codec configured",                     Action::Running, DetailFrom::After},
    {DebugLogModel::STEP_CONFIG,  "BTstack: Stream established",                   Action::Done,    DetailFrom::After},
    {DebugLogModel::STEP_CONFIG,  "BTstack: Stream configuration timed out",       Action::Failed,  DetailFrom::Whole},
    {DebugLogModel::STEP_CONFIG,  "BTstack: set_config_other failed",              Action::Failed,  DetailFrom::Whole},
    {DebugLogModel::STEP_ENCODER, "A2dpService: encoder initialized, ",            Action::Done,    DetailFrom::After},
    {DebugLogModel::STEP_STREAM,  "BTstack: Streaming started",                    Action::Done,    DetailFrom::None},
    {DebugLogModel::STEP_AUDIO,   "WasapiCapture: Capture started",                Action::Done,    DetailFrom::None},
    {DebugLogModel::STEP_LINK,    "A2dpService: connection lost",                  Action::Running, DetailFrom::Whole},
    {DebugLogModel::STEP_LINK,    "BTstack: Reconnecting to ",                     Action::Detail,  DetailFrom::Whole},
    {DebugLogModel::STEP_LINK,    "A2dpService: reconnect attempt ",               Action::Detail,  DetailFrom::Whole},
    {DebugLogModel::STEP_LINK,    "A2dpService: reconnected on attempt ",          Action::Warning, DetailFrom::Whole},
};

QString now_time() {
    return QTime::currentTime().toString("HH:mm:ss.zzz");
}

/* "Prefix: message" -> "message" */
QString strip_module(const QString &t) {
    int pos = t.indexOf(": ");
    return pos < 0 ? t : t.mid(pos + 2);
}

QString after_needle(const QString &t, const char *needle) {
    int pos = t.indexOf(QLatin1String(needle));
    QString s = pos < 0 ? QString() : t.mid(pos + static_cast<int>(strlen(needle))).trimmed();
    if (s.endsWith("...")) s.chop(3);
    if (s.startsWith("(") && s.endsWith(")")) s = s.mid(1, s.length() - 2);
    return s;
}

DebugLogModel::Level classify(const QString &text, const QString &prefix_level) {
    using Level = DebugLogModel::Level;
    if (prefix_level == "ERROR") return Level::Error;
    if (prefix_level == "WARN") return Level::Warn;
    if (prefix_level == "DEBUG") return Level::Debug;
    if (!prefix_level.isEmpty()) return Level::Info;

    /* Plain fprintf(stderr) lines carry no level: go by wording */
    if (text.contains("state=4 ")) return Level::Error;
    if (text.contains("state=3 ")) return Level::Warn;
    QString lower = text.toLower();
    if (lower.contains("warning") || lower.contains("discontinuity") ||
        lower.contains("not loaded") || lower.contains("connection lost"))
        return Level::Warn;
    if (lower.contains("error") || lower.contains("failed") || lower.contains("timed out") ||
        lower.contains("exception"))
        return Level::Error;
    return Level::Info;
}

bool finished(DebugLogModel::StepStatus s) {
    return s == DebugLogModel::StepStatus::Done || s == DebugLogModel::StepStatus::Warning;
}

} // namespace

const char *DebugLogModel::step_name(Step s) {
    static const char *names[STEP_COUNT] = {
        "Start (profile)", "Bluetooth adapter (BTstack)", "Connect (ACL / AVDTP signaling)",
        "Capability discovery", "Codec selection", "Audio capture (WASAPI)",
        "Stream configuration", "Encoder", "Stream start (AVDTP START)",
        "Audio sending", "Connection upkeep",
    };
    return (s >= 0 && s < STEP_COUNT) ? names[s] : "";
}

const char *DebugLogModel::step_label_key(Step s) {
    static const char *keys[STEP_COUNT] = {
        "dbgc.step.start", "dbgc.step.btstack", "dbgc.step.connect", "dbgc.step.caps",
        "dbgc.step.codec", "dbgc.step.capture", "dbgc.step.config", "dbgc.step.encoder",
        "dbgc.step.stream", "dbgc.step.audio", "dbgc.step.link",
    };
    return (s >= 0 && s < STEP_COUNT) ? keys[s] : "";
}

const char *DebugLogModel::level_name(Level l) {
    switch (l) {
    case Level::Debug: return "DEBUG";
    case Level::Info:  return "INFO";
    case Level::Warn:  return "WARN";
    case Level::Error: return "ERROR";
    }
    return "";
}

DebugLogModel::DebugLogModel(const std::string &path, QObject *parent)
    : QObject(parent)
    , path_(path)
    , timer_(new QTimer(this))
{
    connect(timer_, &QTimer::timeout, this, &DebugLogModel::poll);
    poll();
    timer_->start(POLL_MS);
}

DebugLogModel::~DebugLogModel() {
    if (file_) CloseHandle(static_cast<HANDLE>(file_));
}

const DebugLogModel::Entry *DebugLogModel::find(uint64_t seq) const {
    uint64_t first = first_seq();
    if (seq < first || seq >= next_seq_) return nullptr;
    return &entries_[static_cast<size_t>(seq - first)];
}

void DebugLogModel::poll() {
    if (!file_) {
        /* The CRT opened debug.log for writing with full sharing */
        HANDLE h = CreateFileA(path_.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        file_ = h;
    }
    HANDLE h = static_cast<HANDLE>(file_);

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size)) return;
    uint64_t file_size = static_cast<uint64_t>(size.QuadPart);
    if (file_size < offset_) { /* truncated */
        offset_ = 0;
        partial_.clear();
    }
    bool skip_first_line = false;
    if (first_read_ && file_size > MAX_INITIAL_BYTES) {
        offset_ = file_size - MAX_INITIAL_BYTES;
        skip_first_line = true;
    }
    if (file_size == offset_) {
        first_read_ = false;
        return;
    }

    bool live = !first_read_;
    QString arrival = now_time();
    std::string buf;
    buf.resize(64 * 1024);
    while (offset_ < file_size) {
        LARGE_INTEGER pos{};
        pos.QuadPart = static_cast<LONGLONG>(offset_);
        if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) break;
        DWORD want = static_cast<DWORD>(std::min<uint64_t>(buf.size(), file_size - offset_));
        DWORD got = 0;
        if (!ReadFile(h, &buf[0], want, &got, nullptr) || got == 0) break;
        offset_ += got;

        size_t start = 0;
        for (size_t i = 0; i < got; i++) {
            if (buf[i] != '\n') continue;
            partial_.append(buf, start, i - start);
            start = i + 1;
            if (skip_first_line) {
                skip_first_line = false;
            } else {
                add_line(partial_, arrival, live);
            }
            partial_.clear();
        }
        partial_.append(buf, start, got - start);
    }
    first_read_ = false;
}

void DebugLogModel::add_line(const std::string &raw_in, const QString &arrival_time, bool live) {
    std::string raw = raw_in;
    while (!raw.empty() && (raw.back() == '\r' || raw.back() == '\n')) raw.pop_back();
    if (raw.empty()) return;

    QString line = QString::fromUtf8(raw.c_str());
    if (line.contains(QChar::ReplacementCharacter)) line = QString::fromLocal8Bit(raw.c_str());

    /* LOG_* prefix: "[hh:mm:ss.mmm LEVEL] " */
    Entry e;
    QString prefix_level;
    if (line.length() > 21 && line[0] == '[' && line[3] == ':' && line[19] == ']') {
        e.time = line.mid(1, 12);
        prefix_level = line.mid(14, 5).trimmed();
        line = line.mid(21);
    } else {
        e.time = live ? arrival_time : last_time_;
    }
    last_time_ = e.time;
    e.seq = next_seq_++;
    e.level = classify(line, prefix_level);
    e.text = line;

    track(e);
    e.session = session_.id;
    entries_.push_back(std::move(e));
    while (entries_.size() > MAX_ENTRIES) entries_.pop_front();
}

void DebugLogModel::begin_session(const Entry &e) {
    uint32_t id = session_.id + 1;
    session_ = Session{};
    session_.id = id;
    session_.first_seq = e.seq;
    session_.start_time = e.time;
}

void DebugLogModel::set_step(Step s, StepStatus status, const Entry &e, const QString &detail) {
    FlowStep &st = session_.steps[s];
    switch (status) {
    case StepStatus::Failed:
        if (st.status == StepStatus::Failed) return; /* keep the first cause */
        break;
    case StepStatus::Warning:
        if (st.status == StepStatus::Failed) return;
        break;
    case StepStatus::Done:
        if (st.status == StepStatus::Failed) return;
        if (st.status == StepStatus::Warning) { /* completed, but keep the warning */
            if (!detail.isEmpty()) st.detail = detail;
            return;
        }
        break;
    case StepStatus::Running:
        /* Only the connection upkeep step goes back from finished to running */
        if (st.status == StepStatus::Failed) return;
        if (finished(st.status) && s != STEP_LINK) return;
        break;
    case StepStatus::Pending:
        break;
    }
    st.status = status;
    st.time = e.time;
    st.seq = e.seq;
    if (!detail.isEmpty()) st.detail = detail;
}

void DebugLogModel::set_detail(Step s, const QString &detail) {
    if (!detail.isEmpty()) session_.steps[s].detail = detail;
}

void DebugLogModel::track(const Entry &e) {
    const QString &t = e.text;

    /* ---- Adapter information (independent of connection attempts) ---- */
    if (t.startsWith("BtAdapterEnumerator: USB ")) {
        /* Devices of an enumeration come before its "found" line: keep
         * them (with their indented hints) for the block it starts */
        if (!enum_collecting_) enum_pending_.clear();
        enum_collecting_ = true;
        enum_pending_.push_back(t);
    } else if (enum_collecting_ && t.startsWith("  ")) {
        enum_pending_.push_back(t);
    } else if (t.startsWith("BtAdapterEnumerator: found")) {
        adapter_lines_.erase(std::remove_if(adapter_lines_.begin(), adapter_lines_.end(),
            [](const QString &l) { return l.startsWith("BtAdapterEnumerator") || l.startsWith("  "); }),
            adapter_lines_.end());
        adapter_lines_.insert(adapter_lines_.begin(), t);
        adapter_lines_.insert(adapter_lines_.begin() + 1, enum_pending_.begin(), enum_pending_.end());
        enum_pending_.clear();
        enum_collecting_ = false;
        in_adapter_block_ = true;
    } else if (in_adapter_block_ && (t.startsWith("  ") || t.startsWith("BtAdapterEnumerator: no usable"))) {
        /* Keep the enumeration together, ahead of the lines below */
        auto it = std::find_if(adapter_lines_.begin(), adapter_lines_.end(),
            [](const QString &l) { return !l.startsWith("BtAdapterEnumerator") && !l.startsWith("  "); });
        adapter_lines_.insert(it, t);
    } else {
        in_adapter_block_ = false;
        if (t.startsWith("BTstack: Controller ") || t.startsWith("A2dpService: Using chip") ||
            t.startsWith("BTstack: Realtek adapter PID") || t.startsWith("BTstack: H4 over TCP")) {
            QString key = t.section(' ', 0, 0) + " " + t.section(' ', 1, 1);
            adapter_lines_.erase(std::remove_if(adapter_lines_.begin(), adapter_lines_.end(),
                [&](const QString &l) { return l.startsWith(key); }), adapter_lines_.end());
            adapter_lines_.push_back(t);
        }
    }

    /* ---- Service state ---- */
    int state = -1;
    if (t.startsWith("A2dpService: state=")) {
        bool ok = false;
        int v = t.mid(19).section(' ', 0, 0).toInt(&ok);
        state = ok ? v : -1;
        last_state_ = state;
        last_state_text_ = t.section(' ', 2);
        if (last_state_text_.startsWith("text=")) last_state_text_ = last_state_text_.mid(5);
    }

    /* ---- Connection attempts ---- */
    if (t.startsWith("A2dpService: start_streaming called")) {
        begin_session(e);
        set_step(STEP_START, StepStatus::Done, e, QString());
        return;
    }
    if (session_.id == 0 || !session_.stop_time.isEmpty()) return;

    if (t.startsWith("A2dpService: stop_streaming called")) {
        session_.stop_time = e.time;
        return;
    }

    if (state == 4) {
        /* Error state: blame the first step that did not complete */
        for (int s = 0; s < STEP_COUNT; s++) {
            if (!finished(session_.steps[s].status) || s == STEP_LINK) {
                set_step(static_cast<Step>(s), StepStatus::Failed, e, last_state_text_);
                break;
            }
        }
        return;
    }

    for (const Rule &r : RULES) {
        if (!t.contains(QLatin1String(r.needle))) continue;
        QString detail;
        if (r.detail == DetailFrom::After) detail = after_needle(t, r.needle);
        else if (r.detail == DetailFrom::Whole) detail = strip_module(t);

        switch (r.action) {
        case Action::Done:    set_step(r.step, StepStatus::Done, e, detail); break;
        case Action::Running: set_step(r.step, StepStatus::Running, e, detail); break;
        case Action::Warning: set_step(r.step, StepStatus::Warning, e, detail); break;
        case Action::Failed:  set_step(r.step, StepStatus::Failed, e, detail); break;
        case Action::Detail:  set_detail(r.step, detail); break;
        }

        /* Follow-ups */
        if (r.step == STEP_BTSTACK && r.action == Action::Done &&
            session_.steps[STEP_BTSTACK].detail.isEmpty()) {
            /* BTstack already up from an earlier attempt: show the controller */
            for (const QString &l : adapter_lines_)
                if (l.startsWith("BTstack: Controller "))
                    set_detail(STEP_BTSTACK, after_needle(l, "BTstack: Controller "));
        }
        if (r.step == STEP_AUDIO && r.action == Action::Done &&
            session_.steps[STEP_LINK].status == StepStatus::Pending) {
            set_step(STEP_LINK, StepStatus::Done, e, QString());
        }
        if (r.step == STEP_LINK && r.action == Action::Running) {
            session_.reconnects++;
        }
        break;
    }
}
