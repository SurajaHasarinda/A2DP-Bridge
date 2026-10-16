/*
 * Theme implementation
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleHints>

namespace {

struct Palette {
    QColor bg, card, card_hover, border, text, text_secondary, text_muted, input;
    QColor idle, connecting, streaming, reconnecting, error, warning, warning_bg, ok;
};

const Palette LIGHT = {
    "#F3F3F3", "#FFFFFF", "#F7F7F7", "#E0E0E0", "#1A1A1A", "#5C5C5C", "#8A8A8A", "#FFFFFF",
    "#6E6E6E", "#B26A00", "#0F7B0F", "#B26A00", "#C42B1C", "#9D5D00", "#FFF4CE", "#0F7B0F",
};

const Palette DARK = {
    "#202020", "#2B2B2B", "#333333", "#3C3C3C", "#FFFFFF", "#C8C8C8", "#8F8F8F", "#333333",
    "#9A9A9A", "#FCC046", "#6CCB5F", "#FCC046", "#FF99A4", "#FCC046", "#433519", "#6CCB5F",
};

/* Brand blue, the same as the app icon */
QColor brand_accent() { return QColor("#2563EB"); }

/* Text colour that reads on the given background */
QColor on_color(const QColor &bg) { return bg.lightness() > 150 ? QColor("#000000") : QColor("#FFFFFF"); }

QString hex(const QColor &c) { return c.name(QColor::HexRgb); }

/* Combo / spin box arrows: QSS can only point at an image file, so a chevron in
 * the current text colour is drawn once and kept in the cache folder */
QString chevron(bool up, const QColor &c) {
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    dir.mkpath(".");
    const QString path = dir.filePath(QString("chevron-%1-%2.png").arg(up ? "up" : "down", c.name().mid(1)));
    if (!QFile::exists(path)) {
        QImage img(32, 32, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(c, 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const qreal y1 = up ? 20 : 12, y2 = up ? 12 : 20;
        p.drawPolyline(QPolygonF{{8, y1}, {16, y2}, {24, y1}});
        p.end();
        img.save(path);
    }
    return QDir::fromNativeSeparators(path);
}

} // namespace

Theme &Theme::instance() {
    static Theme t;
    return t;
}

Theme::Theme() {
    const QStringList families = QFontDatabase::families();
    icon_family_ = families.contains("Segoe Fluent Icons") ? "Segoe Fluent Icons" : "Segoe MDL2 Assets";
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { apply(); });
}

Theme::Mode Theme::mode_from_string(const std::string &s) {
    if (s == "light") return Mode::Light;
    if (s == "system") return Mode::System;
    return Mode::Dark;
}

std::string Theme::mode_to_string(Mode m) {
    switch (m) {
    case Mode::Light: return "light";
    case Mode::System: return "system";
    default: return "dark";
    }
}

bool Theme::is_dark() const { return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark; }

void Theme::set_mode(Mode m) {
    mode_ = m;
    /* Unknown = follow Windows; Qt then reports the system scheme and emits
     * colorSchemeChanged when it changes. Forcing a scheme also darkens or
     * lightens the native title bar. */
    QGuiApplication::styleHints()->setColorScheme(m == Mode::Light  ? Qt::ColorScheme::Light
                                                  : m == Mode::Dark ? Qt::ColorScheme::Dark
                                                                    : Qt::ColorScheme::Unknown);
    apply();
}

QColor Theme::color(Role r) const {
    const Palette &p = is_dark() ? DARK : LIGHT;
    switch (r) {
    case Role::Bg: return p.bg;
    case Role::Card: return p.card;
    case Role::CardHover: return p.card_hover;
    case Role::Border: return p.border;
    case Role::Text: return p.text;
    case Role::TextSecondary: return p.text_secondary;
    case Role::TextMuted: return p.text_muted;
    case Role::Accent: return brand_accent();
    case Role::AccentText: return on_color(brand_accent());
    case Role::Input: return p.input;
    case Role::StatusIdle: return p.idle;
    case Role::StatusConnecting: return p.connecting;
    case Role::StatusStreaming: return p.streaming;
    case Role::StatusReconnecting: return p.reconnecting;
    case Role::StatusError: return p.error;
    case Role::Warning: return p.warning;
    case Role::WarningBg: return p.warning_bg;
    case Role::Ok: return p.ok;
    }
    return p.text;
}

QFont Theme::icon_font(int pixel_size) const {
    QFont f(icon_family_);
    f.setPixelSize(pixel_size);
    return f;
}

void Theme::apply() {
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app) return;

    QPalette pal;
    const QColor accent = color(Role::Accent);
    pal.setColor(QPalette::Window, color(Role::Bg));
    pal.setColor(QPalette::WindowText, color(Role::Text));
    pal.setColor(QPalette::Base, color(Role::Input));
    pal.setColor(QPalette::AlternateBase, color(Role::Card));
    pal.setColor(QPalette::Text, color(Role::Text));
    pal.setColor(QPalette::Button, color(Role::Card));
    pal.setColor(QPalette::ButtonText, color(Role::Text));
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, color(Role::AccentText));
    pal.setColor(QPalette::ToolTipBase, color(Role::Card));
    pal.setColor(QPalette::ToolTipText, color(Role::Text));
    pal.setColor(QPalette::PlaceholderText, color(Role::TextMuted));
    pal.setColor(QPalette::Link, accent);
    pal.setColor(QPalette::Accent, accent);
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        pal.setColor(QPalette::Disabled, role, color(Role::TextMuted));
    app->setPalette(pal);

    QFile f(":/ui/app.qss");
    QString qss;
    if (f.open(QIODevice::ReadOnly)) qss = QString::fromUtf8(f.readAll());
    const std::pair<const char *, Role> tokens[] = {
        {"@bg@", Role::Bg},           {"@card@", Role::Card},       {"@cardHover@", Role::CardHover},
        {"@border@", Role::Border},   {"@text@", Role::Text},       {"@textSecondary@", Role::TextSecondary},
        {"@textMuted@", Role::TextMuted}, {"@input@", Role::Input}, {"@accent@", Role::Accent},
        {"@accentText@", Role::AccentText}, {"@error@", Role::StatusError}, {"@warning@", Role::Warning},
        {"@warningBg@", Role::WarningBg},   {"@ok@", Role::Ok},
    };
    for (const auto &t : tokens) qss.replace(QLatin1String(t.first), hex(color(t.second)));
    qss.replace("@chevronDown@", chevron(false, color(Role::TextSecondary)));
    qss.replace("@chevronUp@", chevron(true, color(Role::TextSecondary)));
    /* Hover / pressed shades of the accent, derived so they work for any accent */
    qss.replace("@accentHover@", hex(accent.lighter(112)));
    qss.replace("@accentPressed@", hex(accent.darker(115)));
    app->setStyleSheet(qss);

    emit changed();
}
