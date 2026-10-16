/*
 * Small helpers shared by the Qt UI: localized strings, printf-style
 * formatting of the language-file templates, URL / folder opening.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#ifndef UI_UTIL_H
#define UI_UTIL_H

#include "localization.h"

#include <QByteArray>
#include <QDesktopServices>
#include <QString>
#include <QUrl>

#include <string>
#include <utility>

/* Localized text for a language-file key */
inline QString U(const char *key) { return QString::fromUtf8(L(key)); }
inline QString U8(const std::string &s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
inline std::string to_std(const QString &s) { return s.toStdString(); }

namespace ui_detail {
/* Arguments of a language-file template: QString / std::string become UTF-8
 * C strings, numbers pass through. The QByteArray temporaries live until the
 * end of the full expression that calls asprintf. */
inline QByteArray cv(const QString &s) { return s.toUtf8(); }
inline QByteArray cv(const std::string &s) { return QByteArray(s.data(), static_cast<qsizetype>(s.size())); }
inline QByteArray cv(const char *s) { return QByteArray(s); }
template <class T> inline const T &cv(const T &v) { return v; }
inline const char *raw(const QByteArray &b) { return b.constData(); }
template <class T> inline T raw(const T &v) { return v; }
} // namespace ui_detail

/* printf-style template from the language file ("%s", "%.1f", "%u", "%llu") */
template <class... A> QString fmt(const char *key, const A &...a) {
    return QString::asprintf(L(key), ui_detail::raw(ui_detail::cv(a))...);
}

/* The same with a literal format string */
template <class... A> QString fmt_lit(const char *format, const A &...a) {
    return QString::asprintf(format, ui_detail::raw(ui_detail::cv(a))...);
}

inline void open_url(const QString &url) { QDesktopServices::openUrl(QUrl(url)); }

#endif /* UI_UTIL_H */
