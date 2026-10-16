/*
 * Main window - status card, volume, profile list, tray icon
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef UI_MAIN_WINDOW_H
#define UI_MAIN_WINDOW_H

#include "backend.h"

#include <QMainWindow>
#include <QSystemTrayIcon>

#include <memory>

class DebugLogModel;
class QFrame;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QMenu;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Backend *backend);
    ~MainWindow() override;

    Backend &backend() { return *backend_; }
    DebugLogModel *debug_log() { return debug_log_.get(); }
    /* Debug mode as of startup: the Debug items follow it */
    bool debug_active() const { return debug_active_; }

    /* Bring the window back from the tray / minimized (a second launch) */
    void activate();
    void retranslate();
    /* Really exit (the close button only hides to the tray) */
    void quit_app();

protected:
    void closeEvent(QCloseEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    void build_ui();
    void build_tray();
    QMenu *build_overflow_menu();
    void rebuild_profile_list();
    void update_status();
    void update_volume_row();
    void update_title();
    void update_tray_tooltip();
    void new_profile();
    void edit_profile(int index);
    void delete_profile(int index);
    void open_settings();
    void open_firmware();
    QString selected_device_name() const;

    Backend *backend_;
    bool debug_active_ = false;
    std::unique_ptr<DebugLogModel> debug_log_;

    /* Header */
    QLabel *title_ = nullptr;
    QToolButton *settings_btn_ = nullptr;
    QToolButton *more_btn_ = nullptr;

    /* Firmware warning */
    QFrame *firmware_bar_ = nullptr;
    QLabel *firmware_text_ = nullptr;
    QPushButton *firmware_btn_ = nullptr;

    /* Status card */
    QLabel *pill_ = nullptr;
    QLabel *device_label_ = nullptr;
    QLabel *codec_label_ = nullptr;
    QLabel *fallback_label_ = nullptr;
    QLabel *info_label_ = nullptr;
    QPushButton *disconnect_btn_ = nullptr;

    /* Volume row */
    QFrame *volume_card_ = nullptr;
    QLabel *volume_label_ = nullptr;
    QSlider *volume_slider_ = nullptr;
    QLabel *volume_value_ = nullptr;
    QTimer *volume_timer_ = nullptr;
    unsigned long volume_touched_tick_ = 0;

    /* Profile list */
    QVBoxLayout *profile_layout_ = nullptr;
    QWidget *profile_container_ = nullptr;
    QPushButton *add_btn_ = nullptr;

    QSystemTrayIcon *tray_ = nullptr;
    bool first_minimize_shown_ = false;
    bool quitting_ = false;
};

#endif /* UI_MAIN_WINDOW_H */
