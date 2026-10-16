/*
 * Link quality window - bitrate, Bluetooth airtime and losses, once a second
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef UI_LINK_QUALITY_DIALOG_H
#define UI_LINK_QUALITY_DIALOG_H

#include <QDialog>

#include <cstdint>

class Backend;
class MainWindow;
class QLabel;
class QProgressBar;
class QTimer;

class LinkQualityDialog : public QDialog {
    Q_OBJECT
public:
    /* Opens the window, or raises it when it is already open */
    static void show_for(MainWindow *window);

private:
    struct Sample {
        uint64_t tick_ms = 0, packets = 0, bytes = 0, dropped = 0, capture_frames = 0, capture_dropped = 0;
    };
    LinkQualityDialog(Backend *backend, QWidget *parent);
    static Sample take_sample();
    void refresh();

    Backend *backend_;
    QTimer *timer_;

    QLabel *idle_label_;
    QLabel *codec_value_, *bitrate_value_, *packets_value_, *queue_value_, *radio_value_;
    QLabel *theory_value_, *airtime_value_;
    QProgressBar *theory_gauge_, *airtime_gauge_;
    QLabel *sent_value_, *dropped_value_, *capture_value_;

    Sample baseline_, prev_;
    bool have_prev_ = false;
};

#endif /* UI_LINK_QUALITY_DIALOG_H */
