/*
 * Peer receiver test (Debug menu) - compares what A2DPWB sends with what a
 * measuring receiver (tools/linux_sink) reports it received
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef UI_RECEIVER_DIALOG_H
#define UI_RECEIVER_DIALOG_H

#include "backend.h"
#include "remote_sink_client.h"

#include <QDialog>

#include <cstdint>
#include <string>

class MainWindow;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTimer;

/* Modeless; open with ReceiverDialog::show_for() so only one exists */
class ReceiverDialog : public QDialog {
    Q_OBJECT
public:
    static void show_for(MainWindow *window);

private:
    enum Step { StepDiscover, StepStats, StepConfigure, StepLink, StepCodec, StepAudio, STEP_COUNT };
    enum class StepState { Pending, Running, Ok, Failed };
    struct Sample {
        uint64_t tick_ms = 0, packets = 0, bytes = 0, dropped = 0, capture_frames = 0, capture_dropped = 0;
    };

    explicit ReceiverDialog(MainWindow *window);
    static Sample take_sample();

    void set_value(QLabel *t, const QString &text, bool bad = false);
    void set_step(Step step, StepState state, const QString &detail = QString());
    void reset_measurement_steps();
    void log(const QString &line);

    void apply_afh();
    void toggle_connection();
    void start_stop();
    void reset();
    void refresh();
    void refresh_steps();
    void refresh_status();
    void refresh_sent(bool streaming, const Sample &now);
    void refresh_received();

    MainWindow *window_;
    Backend *backend_;
    QTimer *timer_;

    QLineEdit *addr_;
    QPushButton *connect_btn_, *start_btn_;
    QLabel *status_;
    QComboBox *codec_, *quality_, *rate_, *depth_, *source_, *afh_, *mtu_, *buffer_;
    QLineEdit *afh_channels_;
    QLabel *step_text_[STEP_COUNT];
    StepState step_state_[STEP_COUNT] = {};
    QPlainTextEdit *log_ = nullptr;

    QLabel *codec_s_, *codec_r_, *bitrate_s_, *bitrate_r_, *total_s_, *total_r_, *loss_s_, *loss_r_;
    QLabel *timing_s_, *timing_r_, *buffer_s_, *buffer_r_, *audio_s_, *audio_r_, *errors_s_, *errors_r_;
    QLabel *signal_s_, *signal_r_;

    Sample baseline_, prev_;
    bool have_prev_ = false;
    uint32_t stream_starts_ = 0;
    A2dpService::State last_state_ = A2dpService::State::Idle;

    RemoteSinkStats remote_base_;
    bool have_remote_base_ = false;

    bool measuring_ = false;
    bool service_left_idle_ = false;
    QString failure_;
    QString status_text_;
    QString last_codec_;
    std::string last_service_text_;
    uint64_t measure_start_ms_ = 0, link_ok_ms_ = 0;
    uint32_t requested_mtu_ = 0, requested_buffer_ms_ = 0;
    bool had_stats_at_start_ = false;
    uint32_t stream_id_at_start_ = 0;
};

#endif /* UI_RECEIVER_DIALOG_H */
