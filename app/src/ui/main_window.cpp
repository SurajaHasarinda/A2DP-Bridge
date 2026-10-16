/*
 * Main window implementation
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "main_window.h"

#include "about_dialog.h"
#include "btstack_transport.h"
#include "debug_console_dialog.h"
#include "debug_log_model.h"
#include "firmware_dialog.h"
#include "hci_capture.h"
#include "link_quality_dialog.h"
#include "profile_dialog.h"
#include "receiver_dialog.h"
#include "settings_dialog.h"
#include "system_integration.h"
#include "theme.h"
#include "ui_util.h"
#include "zadig_helper.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDate>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <windows.h>

#ifndef APP_VERSION
#define APP_VERSION "1.0.1"
#endif

namespace {

/* A label that can be clicked */
class ClickLabel : public QLabel {
    Q_OBJECT
public:
    using QLabel::QLabel;
signals:
    void clicked();
protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) emit clicked();
        QLabel::mouseReleaseEvent(e);
    }
};

void repolish(QWidget *w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

QToolButton *glyph_button(QWidget *parent, const char16_t *g, const QString &tip, int px = 16) {
    auto *b = new QToolButton(parent);
    b->setFont(TH().icon_font(px));
    b->setText(QString::fromUtf16(g));
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setAutoRaise(true);
    return b;
}

/* "LDAC / High(990kbps) / 96kHz / 24bit" */
QString profile_detail(const ConnectionProfile &p) {
    QString codec;
    if (p.codec == "ldac") codec = "LDAC";
    else if (p.codec == "aptxhd") codec = "aptX HD";
    else if (p.codec == "aptxll") codec = "aptX LL";
    else if (p.codec == "aptx") codec = "aptX";
    else if (p.codec == "sbc") codec = "SBC";
    else if (p.codec == "aac") codec = "AAC";
    else if (p.codec == "lhdcv5") codec = "LHDC V5";
    else if (p.codec == "auto") codec = U("codec.auto");
    else codec = U8(p.codec);

    /* "mhq" (900 kbps) exists for LHDC V5 only; other codecs run it as High */
    bool is_mhq = (p.quality == "mhq");
    QString q_name = (p.quality == "hq")                        ? U("quality.high")
                   : (p.quality == "sq")                        ? U("quality.standard")
                   : (p.quality == "mq")                        ? U("quality.mobile")
                   : (is_mhq && p.codec == "lhdcv5")            ? U("quality.medium_high")
                                                                : U("quality.high");
    int q_idx = (p.quality == "sq") ? 1 : (p.quality == "mq") ? 2 : 0;
    bool is_441 = (p.sample_rate == 44100 || p.sample_rate == 88200);
    QString quality = q_name;
    if (p.codec == "ldac") {
        static const int r48[] = {990, 660, 330}, r44[] = {909, 606, 303};
        quality = QString("%1 (%2 kbps)").arg(q_name).arg((is_441 ? r44 : r48)[q_idx]);
    } else if (p.codec == "aac") {
        static const int r[] = {256, 192, 128};
        quality = QString("%1 (%2 kbps)").arg(q_name).arg(r[q_idx]);
    } else if (p.codec == "sbc") {
        static const char *r[] = {"~345", "~249", "~153"};
        quality = QString("%1 (%2 kbps)").arg(q_name, r[q_idx]);
    } else if (p.codec == "lhdcv5") {
        static const int r48[] = {1000, 500, 400}, r44[] = {1000, 480, 400};
        /* 88.2 kHz runs as 96 kHz for LHDC V5, so only 44.1 kHz uses 480 */
        int kbps = is_mhq ? 900 : (p.sample_rate == 44100 ? r44 : r48)[q_idx];
        quality = QString("%1 (%2 kbps)").arg(q_name).arg(kbps);
    }
    QString sr = p.sample_rate > 0 ? QString("%1 kHz").arg(p.sample_rate / 1000.0, 0, 'g', 4) : "Auto";
    QString bd = p.bit_depth > 0 ? QString("%1 bit").arg(p.bit_depth) : "Auto";
    return QString("%1  ·  %2  ·  %3  ·  %4").arg(codec, quality, sr, bd);
}

/* One saved connection profile */
class ProfileCard : public QFrame {
    Q_OBJECT
public:
    ProfileCard(const ConnectionProfile &p, bool active, QWidget *parent) : QFrame(parent) {
        setObjectName("profileCard");
        setProperty("active", active);
        setCursor(Qt::PointingHandCursor);
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(14, 10, 8, 10);
        row->setSpacing(4);

        auto *text = new QVBoxLayout;
        text->setSpacing(2);
        auto *name = new QLabel(U8(p.device_name.empty() ? p.device_address : p.device_name), this);
        name->setProperty("heading", true);
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *detail = new QLabel(profile_detail(p), this);
        detail->setProperty("secondary", true);
        detail->setAttribute(Qt::WA_TransparentForMouseEvents);
        text->addWidget(name);
        text->addWidget(detail);
        row->addLayout(text, 1);

        auto *edit = glyph_button(this, glyph::Edit, U("profile.edit"));
        auto *del = glyph_button(this, glyph::Delete, U("profile.delete"));
        del->setProperty("danger", true);
        row->addWidget(edit);
        row->addWidget(del);
        connect(edit, &QToolButton::clicked, this, &ProfileCard::edit_clicked);
        connect(del, &QToolButton::clicked, this, &ProfileCard::delete_clicked);
    }
signals:
    void connect_clicked();
    void edit_clicked();
    void delete_clicked();
protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) emit connect_clicked();
        QFrame::mouseReleaseEvent(e);
    }
};

} // namespace

/* ======================================================================== */
/* Construction                                                              */
/* ======================================================================== */

MainWindow::MainWindow(Backend *backend) : QMainWindow(nullptr), backend_(backend) {
    setWindowTitle(QString("A2DP Bridge v%1").arg(APP_VERSION));
    resize(440, 640);
    setMinimumSize(400, 460);

    /* Debug mode changes apply after a restart; the Debug items follow the boot state */
    debug_active_ = backend_->settings().debug_mode;
    if (debug_active_)
        debug_log_ = std::make_unique<DebugLogModel>(backend_->service().get_config_dir() + "\\debug.log");

    build_ui();
    build_tray();

    connect(backend_, &Backend::state_changed, this, [this] { update_status(); rebuild_profile_list(); });
    connect(backend_, &Backend::stream_info_changed, this, &MainWindow::update_status);
    connect(&TH(), &Theme::changed, this, [this] { rebuild_profile_list(); update_status(); });

    if (backend_->settings().start_with_windows) RegisterStartup("--minimized");

    update_status();
    rebuild_profile_list();
}

MainWindow::~MainWindow() {
    if (tray_) tray_->hide();
}

void MainWindow::build_ui() {
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(16, 12, 16, 16);
    root->setSpacing(12);

    /* ---- Header ---- */
    auto *header = new QHBoxLayout;
    auto *logo = new QLabel(this);
    logo->setPixmap(QApplication::windowIcon().pixmap(28, 28));
    header->addWidget(logo);
    header->addSpacing(4);
    title_ = new QLabel("A2DP Bridge", this);
    title_->setProperty("title", true);
    header->addWidget(title_);
    header->addStretch(1);
    settings_btn_ = glyph_button(this, glyph::Settings, U("menu.settings"), 18);
    connect(settings_btn_, &QToolButton::clicked, this, &MainWindow::open_settings);
    header->addWidget(settings_btn_);
    more_btn_ = glyph_button(this, glyph::More, U("menu.more"), 18);
    more_btn_->setPopupMode(QToolButton::InstantPopup);
    more_btn_->setMenu(build_overflow_menu());
    header->addWidget(more_btn_);
    root->addLayout(header);

    /* ---- Firmware warning ---- */
    firmware_bar_ = new QFrame(this);
    firmware_bar_->setObjectName("warnBar");
    auto *fw = new QHBoxLayout(firmware_bar_);
    fw->setContentsMargins(12, 8, 8, 8);
    firmware_text_ = new QLabel(U("firmware.missing_short"), firmware_bar_);
    firmware_text_->setWordWrap(true);
    fw->addWidget(firmware_text_, 1);
    firmware_btn_ = new QPushButton(U("firmware.setup"), firmware_bar_);
    connect(firmware_btn_, &QPushButton::clicked, this, &MainWindow::open_firmware);
    fw->addWidget(firmware_btn_);
    firmware_bar_->setVisible(!backend_->service().is_firmware_present());
    root->addWidget(firmware_bar_);

    /* ---- Status card ---- */
    auto *status = new QFrame(this);
    status->setObjectName("card");
    auto *sv = new QVBoxLayout(status);
    sv->setContentsMargins(14, 12, 14, 12);
    sv->setSpacing(8);

    auto *top = new QHBoxLayout;
    pill_ = new QLabel(status);
    pill_->setObjectName("pill");
    top->addWidget(pill_, 0, Qt::AlignVCenter);
    device_label_ = new QLabel(status);
    device_label_->setProperty("heading", true);
    top->addWidget(device_label_, 1);
    disconnect_btn_ = new QPushButton(U("connection.disconnect"), status);
    disconnect_btn_->setCursor(Qt::PointingHandCursor);
    connect(disconnect_btn_, &QPushButton::clicked, this, [this] { backend_->service().stop_streaming(); });
    top->addWidget(disconnect_btn_);
    sv->addLayout(top);

    auto *codec = new ClickLabel(status);
    codec_label_ = codec;
    codec_label_->setCursor(Qt::PointingHandCursor);
    connect(codec, &ClickLabel::clicked, this, [this] {
        if (backend_->state() == A2dpService::State::Streaming) LinkQualityDialog::show_for(this);
    });
    sv->addWidget(codec_label_);
    fallback_label_ = new QLabel(status);
    fallback_label_->setProperty("warning", true);
    fallback_label_->setWordWrap(true);
    sv->addWidget(fallback_label_);

    auto *info = new ClickLabel(status);
    info_label_ = info;
    info_label_->setWordWrap(true);
    connect(info, &ClickLabel::clicked, this, [this] {
        if (backend_->state() == A2dpService::State::Error && !backend_->status_text().empty())
            QApplication::clipboard()->setText(U8(backend_->status_text()));
    });
    sv->addWidget(info_label_);
    root->addWidget(status);

    /* ---- Headphone volume (AVRCP absolute volume, while streaming) ---- */
    volume_card_ = new QFrame(this);
    volume_card_->setObjectName("card");
    auto *vh = new QHBoxLayout(volume_card_);
    vh->setContentsMargins(14, 8, 14, 8);
    vh->setSpacing(10);
    volume_label_ = new QLabel(QString::fromUtf16(glyph::Volume), volume_card_);
    volume_label_->setFont(TH().icon_font(18));
    volume_label_->setToolTip(U("tooltip.volume"));
    vh->addWidget(volume_label_);
    volume_slider_ = new QSlider(Qt::Horizontal, volume_card_);
    volume_slider_->setRange(0, 100);
    volume_slider_->setToolTip(U("tooltip.volume"));
    vh->addWidget(volume_slider_, 1);
    volume_value_ = new QLabel("100%", volume_card_);
    volume_value_->setMinimumWidth(40);
    volume_value_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    vh->addWidget(volume_value_);
    volume_card_->hide();
    root->addWidget(volume_card_);

    connect(volume_slider_, &QSlider::valueChanged, this, [this](int percent) {
        volume_value_->setText(QString("%1%").arg(percent));
        /* update_volume_row() blocks this signal when it follows the headphones */
        volume_touched_tick_ = GetTickCount();
        BtStackTransport::request_volume(static_cast<uint8_t>((percent * 127 + 50) / 100));
    });
    /* Follow the headphones (their buttons, or the value at connect) */
    volume_timer_ = new QTimer(this);
    connect(volume_timer_, &QTimer::timeout, this, &MainWindow::update_volume_row);
    volume_timer_->start(300);

    /* ---- Profile list (scrollable) ---- */
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    profile_container_ = new QWidget;
    profile_container_->setAttribute(Qt::WA_StyledBackground, false);
    profile_layout_ = new QVBoxLayout(profile_container_);
    profile_layout_->setContentsMargins(0, 0, 0, 0);
    profile_layout_->setSpacing(8);
    profile_layout_->addStretch(1);
    scroll->setWidget(profile_container_);
    root->addWidget(scroll, 1);

    add_btn_ = new QPushButton("+  " + U("profile.new"), this);
    add_btn_->setProperty("accent", true);
    add_btn_->setCursor(Qt::PointingHandCursor);
    connect(add_btn_, &QPushButton::clicked, this, &MainWindow::new_profile);
    root->addWidget(add_btn_);
}

QMenu *MainWindow::build_overflow_menu() {
    auto *menu = new QMenu(this);
    menu->addAction(U("menu.file.link_quality"), this, [this] { LinkQualityDialog::show_for(this); });
    menu->addAction(U("firmware.title"), this, &MainWindow::open_firmware);
    menu->addAction(U("menu.file.open_config"), this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(U8(backend_->service().get_config_dir())));
    });

    if (debug_active_) {
        menu->addSeparator();
        QMenu *dbg = menu->addMenu(U("menu.debug"));
        QAction *console = dbg->addAction(U("debug.console"), this,
                                          [this] { DebugConsoleDialog::show_for(this, debug_log_.get()); });
        console->setShortcut(QKeySequence("Ctrl+Shift+D"));
        addAction(console); /* works with the menu closed */
        dbg->addSeparator();
        QAction *cap_start = dbg->addAction(U("debug.capture_start"));
        QAction *cap_stop = dbg->addAction(U("debug.capture_stop"));
        connect(dbg, &QMenu::aboutToShow, this, [cap_start, cap_stop] {
            cap_start->setEnabled(!hci_capture::active());
            cap_stop->setEnabled(hci_capture::active());
        });
        connect(cap_start, &QAction::triggered, this, [this] {
            SYSTEMTIME st;
            GetLocalTime(&st);
            char stamp[32];
            snprintf(stamp, sizeof(stamp), "%04u%02u%02u_%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
                     st.wMinute, st.wSecond);
            std::string path = backend_->service().get_config_dir() + "\\hci_" + stamp + ".pklg";
            if (!hci_capture::start(path))
                QMessageBox::critical(this, U("menu.debug"), fmt("debug.capture_failed", path));
            update_title();
        });
        connect(cap_stop, &QAction::triggered, this, [this] {
            hci_capture::stop();
            update_title();
            std::string path = hci_capture::path();
            double mb = hci_capture::bytes_written() / (1024.0 * 1024.0);
            auto answer = QMessageBox::question(this, U("menu.debug"), fmt("debug.capture_saved", path, mb));
            if (answer == QMessageBox::Yes)
                QProcess::startDetached("explorer.exe", {"/select," + QDir::toNativeSeparators(U8(path))});
        });
        dbg->addSeparator();
        dbg->addAction(U("debug.receiver"), this, [this] { ReceiverDialog::show_for(this); });
        dbg->addSeparator();
        dbg->addAction(U("debug.open_log"), this, [this] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(U8(backend_->service().get_config_dir() + "\\debug.log")));
        });
    }

    menu->addSeparator();
    menu->addAction(U("zadig.download"), this, [] { ZadigHelper::open_zadig_website(); });
    menu->addAction(U("vbcable.download"), this, [] { open_url("https://vb-audio.com/Cable/"); });
    menu->addAction(U("help.about"), this, [this] { AboutDialog(this).exec(); });
    menu->addSeparator();
    menu->addAction(U("menu.exit"), this, &MainWindow::quit_app);
    return menu;
}

void MainWindow::build_tray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    tray_ = new QSystemTrayIcon(windowIcon(), this);
    tray_->setToolTip(windowTitle());
    auto *menu = new QMenu(this);
    /* Rebuilt each time it opens: the profile list and state change */
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        menu->clear();
        const auto &profs = backend_->profiles().profiles();
        for (int i = 0; i < static_cast<int>(profs.size()) && i < 10; i++) {
            QString label = U8(profs[i].device_name.empty() ? profs[i].device_address : profs[i].device_name);
            if (i == backend_->selected_profile()) label = QString::fromUtf16(u"▶ ") + label;
            menu->addAction(label, this, [this, i] {
                backend_->start_profile(i);
                activate();
            });
        }
        if (!profs.empty()) menu->addSeparator();
        QAction *disc = menu->addAction(U("connection.disconnect"), this,
                                        [this] { backend_->service().stop_streaming(); });
        disc->setEnabled(backend_->state() == A2dpService::State::Streaming);
        menu->addSeparator();
        menu->addAction(U("tray.exit"), this, &MainWindow::quit_app);
    });
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) activate();
    });
    tray_->show();
}

/* ======================================================================== */
/* Display                                                                   */
/* ======================================================================== */

QString MainWindow::selected_device_name() const {
    const int sel = backend_->selected_profile();
    const auto &profs = backend_->profiles().profiles();
    if (sel < 0 || sel >= static_cast<int>(profs.size())) return {};
    const ConnectionProfile &p = profs[sel];
    return U8(p.device_name.empty() ? p.device_address : p.device_name);
}

void MainWindow::update_status() {
    const auto state = backend_->state();
    const auto &info = backend_->stream_info();
    const std::string &text = backend_->status_text();

    const char *key, *prop;
    switch (state) {
    case A2dpService::State::Idle:         key = "status.idle";         prop = "idle"; break;
    case A2dpService::State::Connecting:   key = "status.connecting";   prop = "connecting"; break;
    case A2dpService::State::Streaming:    key = "status.streaming";    prop = "streaming"; break;
    case A2dpService::State::Reconnecting: key = "status.reconnecting"; prop = "reconnecting"; break;
    default:                               key = "status.error";        prop = "error"; break;
    }
    pill_->setText(U(key));
    pill_->setProperty("state", prop);
    repolish(pill_);

    const bool streaming = state == A2dpService::State::Streaming;
    device_label_->setText(streaming ? selected_device_name() : QString());
    disconnect_btn_->setVisible(streaming || state == A2dpService::State::Connecting ||
                                state == A2dpService::State::Reconnecting);

    /* Codec in use, and why it is not the profile's (while streaming) */
    const bool show_codec = streaming && !info.codec.empty();
    codec_label_->setVisible(show_codec);
    fallback_label_->setVisible(show_codec && !info.requested_codec.empty());
    if (show_codec) {
        codec_label_->setText(fmt("status.codec_info", info.codec, info.sample_rate / 1000.0));
        if (info.requested_codec.empty()) {
            codec_label_->setToolTip(U("quality.title"));
            fallback_label_->setToolTip({});
        } else {
            fallback_label_->setText(
                fmt(info.requested_in_use ? "status.codec_fallback_in_use" : "status.codec_fallback",
                    info.requested_codec));
            QString tip = fmt("tooltip.codec_fallback_help", info.requested_codec, info.codec);
            codec_label_->setToolTip(tip);
            fallback_label_->setToolTip(tip);
        }
    }

    if (state == A2dpService::State::Connecting || state == A2dpService::State::Reconnecting) {
        info_label_->setText(U8(text));
        info_label_->setProperty("error", false);
        info_label_->setToolTip({});
        info_label_->setCursor(Qt::ArrowCursor);
    } else if (state == A2dpService::State::Error) {
        info_label_->setText(U8(text));
        info_label_->setProperty("error", true);
        info_label_->setToolTip(U8(text) + "\n" + U("error.click_to_copy_hint"));
        info_label_->setCursor(Qt::PointingHandCursor);
    } else {
        info_label_->clear();
        info_label_->setProperty("error", false);
        info_label_->setToolTip({});
    }
    repolish(info_label_);
    info_label_->setVisible(!info_label_->text().isEmpty());

    update_tray_tooltip();
}

void MainWindow::update_tray_tooltip() {
    if (!tray_) return;
    const bool streaming = backend_->state() == A2dpService::State::Streaming;
    QString tip = U(streaming ? "tray.tooltip_streaming" : "tray.tooltip_idle");
    if (streaming) {
        QString name = selected_device_name();
        if (!name.isEmpty()) tip += " (" + name + ")";
    }
    tray_->setToolTip(tip);
}

void MainWindow::update_volume_row() {
    backend_->save_remembered_volumes(false);
    const int vol = BtStackTransport::remote_volume();
    const bool show = backend_->state() == A2dpService::State::Streaming && vol >= 0;
    if (volume_card_->isVisible() != show) volume_card_->setVisible(show);
    if (!show) return;
    /* Leave the slider alone while the user is moving it */
    if (volume_slider_->isSliderDown() || GetTickCount() - volume_touched_tick_ < 1500) return;
    const int percent = (vol * 100 + 63) / 127;
    if (volume_slider_->value() != percent) {
        QSignalBlocker b(volume_slider_);
        volume_slider_->setValue(percent);
        volume_value_->setText(QString("%1%").arg(percent));
    }
}

void MainWindow::update_title() {
    QString title = QString("A2DP Bridge v%1").arg(APP_VERSION);
    if (hci_capture::active()) title += U("debug.capture_title_suffix");
    setWindowTitle(title);
}

void MainWindow::rebuild_profile_list() {
    /* Keep the trailing stretch, drop the cards */
    while (profile_layout_->count() > 1) {
        QLayoutItem *item = profile_layout_->takeAt(0);
        if (QWidget *w = item->widget()) w->deleteLater();
        delete item;
    }
    const auto &profs = backend_->profiles().profiles();
    const bool streaming = backend_->state() == A2dpService::State::Streaming ||
                           backend_->state() == A2dpService::State::Connecting ||
                           backend_->state() == A2dpService::State::Reconnecting;
    if (profs.empty()) {
        /* Empty state: the logo, the hint, and the button below is the obvious next step */
        auto *empty = new QWidget(profile_container_);
        auto *ev = new QVBoxLayout(empty);
        ev->setContentsMargins(0, 48, 0, 0);
        ev->setSpacing(14);
        auto *icon = new QLabel(empty);
        icon->setPixmap(QApplication::windowIcon().pixmap(64, 64));
        icon->setAlignment(Qt::AlignCenter);
        ev->addWidget(icon);
        auto *hint = new QLabel(U("profile.empty"), empty);
        hint->setProperty("muted", true);
        hint->setWordWrap(true);
        hint->setAlignment(Qt::AlignCenter);
        ev->addWidget(hint);
        profile_layout_->insertWidget(0, empty);
        return;
    }
    for (int i = 0; i < static_cast<int>(profs.size()); i++) {
        auto *card = new ProfileCard(profs[i], streaming && backend_->selected_profile() == i, profile_container_);
        connect(card, &ProfileCard::connect_clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] { backend_->start_profile(i); });
        });
        connect(card, &ProfileCard::edit_clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] { edit_profile(i); });
        });
        connect(card, &ProfileCard::delete_clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] { delete_profile(i); });
        });
        profile_layout_->insertWidget(i, card);
    }
}

/* ======================================================================== */
/* Actions                                                                   */
/* ======================================================================== */

void MainWindow::new_profile() {
    ProfileDialog dlg(backend_, -1, this);
    if (dlg.exec() == QDialog::Accepted) rebuild_profile_list();
}

void MainWindow::edit_profile(int index) {
    ProfileDialog dlg(backend_, index, this);
    if (dlg.exec() != QDialog::Accepted) return;
    rebuild_profile_list();
    /* If the edited profile is currently streaming, reconnect */
    const auto state = backend_->service().state();
    if (index == backend_->selected_profile() &&
        (state == A2dpService::State::Streaming || state == A2dpService::State::Connecting))
        backend_->start_profile(index);
}

void MainWindow::delete_profile(int index) {
    if (QMessageBox::warning(this, U("modal.confirm_title"), U("modal.confirm_delete_profile"),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    backend_->profiles().remove(index);
    backend_->profile_removed(index);
    rebuild_profile_list();
}

void MainWindow::open_settings() {
    bool again;
    do {
        SettingsDialog dlg(backend_, this);
        connect(&dlg, &SettingsDialog::language_changed, this, &MainWindow::retranslate);
        dlg.exec();
        again = dlg.reopen();
    } while (again);
}

void MainWindow::open_firmware() {
    FirmwareDialog dlg(backend_, this);
    dlg.exec();
    if (dlg.chip_changed() || backend_->service().was_firmware_updated()) {
        /* Force BTstack reinit on next connect to pick up new chip/firmware */
        backend_->service().reset_btstack();
    }
    firmware_bar_->setVisible(!backend_->service().is_firmware_present());
}

void MainWindow::retranslate() {
    /* Open windows that are not rebuilt keep the old language: close them */
    for (QDialog *d : findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly))
        if (!d->isModal()) d->close();
    firmware_text_->setText(U("firmware.missing_short"));
    firmware_btn_->setText(U("firmware.setup"));
    disconnect_btn_->setText(U("connection.disconnect"));
    add_btn_->setText("+  " + U("profile.new"));
    settings_btn_->setToolTip(U("menu.settings"));
    more_btn_->setToolTip(U("menu.more"));
    more_btn_->menu()->deleteLater();
    more_btn_->setMenu(build_overflow_menu());
    rebuild_profile_list();
    update_status();
}

/* ======================================================================== */
/* Window events                                                             */
/* ======================================================================== */

void MainWindow::activate() {
    if (isMinimized()) setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    raise();
    activateWindow();
}

void MainWindow::quit_app() {
    quitting_ = true;
    close();
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (!quitting_ && backend_->settings().minimize_to_tray && tray_ && tray_->isVisible()) {
        hide();
        if (!first_minimize_shown_) {
            tray_->showMessage("A2DP Bridge", U("tray.minimized_hint"), QSystemTrayIcon::Information, 15000);
            first_minimize_shown_ = true;
        }
        e->ignore();
        return;
    }
    if (tray_) tray_->hide();
    e->accept();
    qApp->quit();
}

void MainWindow::changeEvent(QEvent *e) {
    if (e->type() == QEvent::WindowStateChange && isMinimized() && backend_->settings().minimize_to_tray && tray_)
        QTimer::singleShot(0, this, [this] { hide(); setWindowState(windowState() & ~Qt::WindowMinimized); });
    QMainWindow::changeEvent(e);
}

#include "main_window.moc"
