/*
 * Theme - light / dark / follow-system, one palette + one stylesheet
 *
 * Fusion style with a QPalette and app.qss (colours filled in from the tokens
 * below), so the look is the same on Windows 10 and 11 and follows the system
 * colour scheme live. The title bar follows the colour scheme (Qt 6.5+).
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef UI_THEME_H
#define UI_THEME_H

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>

#include <string>

enum class Role {
    Bg,            /* window background */
    Card,          /* raised surface */
    CardHover,
    Border,
    Text,
    TextSecondary,
    TextMuted,
    Accent,
    AccentText,    /* text on an accent surface */
    Input,         /* edit / combo background */
    StatusIdle,
    StatusConnecting,
    StatusStreaming,
    StatusReconnecting,
    StatusError,
    Warning,       /* amber: firmware warning, fallback notice */
    WarningBg,
    Ok,
};

class Theme : public QObject {
    Q_OBJECT
public:
    enum class Mode { System, Light, Dark };

    static Theme &instance();

    void set_mode(Mode m);
    Mode mode() const { return mode_; }
    static Mode mode_from_string(const std::string &s);
    static std::string mode_to_string(Mode m);

    bool is_dark() const;
    QColor color(Role r) const;

    /* Glyph font (Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets on 10) */
    QFont icon_font(int pixel_size) const;

signals:
    /* Palette and stylesheet were applied again (mode or system change) */
    void changed();

private:
    Theme();
    void apply();

    Mode mode_ = Mode::System;
    QString icon_family_;
};

inline Theme &TH() { return Theme::instance(); }

/* Icon glyph code points shared by both icon fonts */
namespace glyph {
constexpr const char16_t *Settings = u"";
constexpr const char16_t *More = u"";
constexpr const char16_t *Play = u"";
constexpr const char16_t *Edit = u"";
constexpr const char16_t *Delete = u"";
constexpr const char16_t *Add = u"";
constexpr const char16_t *Volume = u"";
constexpr const char16_t *Chevron = u"";
constexpr const char16_t *Copy = u"";
} // namespace glyph

#endif /* UI_THEME_H */
