/*
 * GUI entry point implementation
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "ui_app.h"

#include "app_settings.h"
#include "backend.h"
#include "config_path.h"
#include "main_window.h"
#include "theme.h"

#include <QAbstractNativeEventFilter>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QSlider>
#include <QWheelEvent>
#include <QFontDatabase>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QStyleFactory>
#include <QSystemTrayIcon>

#include <cstdio>
#include <windows.h>

namespace ui {

const wchar_t *const kActivateMessage = L"A2DPWB_Activate";

namespace {

/* Scrolling over a dropdown, spin box or slider must scroll the page, not change the value.
 * Dropdowns ignore the wheel always; spin boxes and sliders only while they have keyboard focus. */
class WheelGuard : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() != QEvent::Wheel || forwarding_) return false;
        auto *w = qobject_cast<QWidget *>(obj);
        if (!w) return false;
        const bool combo = qobject_cast<QComboBox *>(w) != nullptr;
        /* QSlider, not QAbstractSlider: scroll bars are sliders too, and the scroll area
         * hands its wheel events to them, so guarding them would bounce the event forever */
        const bool value_widget = qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QSlider *>(w);
        if (!combo && !(value_widget && !w->hasFocus())) return false;
        /* Hand the scroll to the enclosing scroll area, if any */
        auto *we = static_cast<QWheelEvent *>(event);
        for (QWidget *p = w->parentWidget(); p; p = p->parentWidget()) {
            if (auto *area = qobject_cast<QAbstractScrollArea *>(p)) {
                QWheelEvent fwd(area->viewport()->mapFromGlobal(we->globalPosition()), we->globalPosition(),
                                we->pixelDelta(), we->angleDelta(), we->buttons(), we->modifiers(),
                                we->phase(), we->inverted());
                forwarding_ = true;
                QCoreApplication::sendEvent(area->viewport(), &fwd);
                forwarding_ = false;
                break;
            }
        }
        return true;
    }

private:
    bool forwarding_ = false;
};

/* Brings the window forward when a second instance asks (see main.cpp) */
class ActivateFilter : public QAbstractNativeEventFilter {
public:
    ActivateFilter(MainWindow *w) : window_(w), message_(RegisterWindowMessageW(kActivateMessage)) {}
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *) override {
        if (type != "windows_generic_MSG") return false;
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message != message_) return false;
        window_->activate();
        return true;
    }

private:
    MainWindow *window_;
    UINT message_;
};

/* The application icon (resource 101 of the exe) at several sizes */
QIcon load_app_icon() {
    QIcon icon;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    for (int size : {16, 20, 24, 32, 48, 64, 256}) {
        HICON h = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(101), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
        if (!h) continue;
        ICONINFO ii{};
        if (GetIconInfo(h, &ii)) {
            BITMAP bm{};
            GetObject(ii.hbmColor, sizeof(bm), &bm);
            QImage img(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = bm.bmWidth;
            bi.bmiHeader.biHeight = -bm.bmHeight; /* top-down */
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            HDC dc = GetDC(nullptr);
            if (GetDIBits(dc, ii.hbmColor, 0, bm.bmHeight, img.bits(), &bi, DIB_RGB_COLORS))
                icon.addPixmap(QPixmap::fromImage(img));
            ReleaseDC(nullptr, dc);
            DeleteObject(ii.hbmColor);
            DeleteObject(ii.hbmMask);
        }
        DestroyIcon(h);
    }
    return icon;
}

} // namespace

int run_gui(int argc, char *argv[], bool start_minimized) {
    /* Redirect stderr based on the debug mode setting */
    {
        AppSettings boot;
        boot.load();
        if (boot.debug_mode) {
            const std::string log_path = get_config_dir() + "\\debug.log";
            freopen(log_path.c_str(), "w", stderr);
            setvbuf(stderr, nullptr, _IONBF, 0);
            fprintf(stderr, "=== A2DPWB started (debug mode) ===\n");
        } else {
            freopen("NUL", "w", stderr);
        }
        freopen("NUL", "w", stdout);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName("A2DPWB");
    QApplication::setQuitOnLastWindowClosed(false); /* the tray keeps it alive; MainWindow quits explicitly */
    QApplication::setStyle(QStyleFactory::create("fusion"));

    const QStringList families = QFontDatabase::families();
    QFont font(families.contains("Segoe UI Variable Text") ? "Segoe UI Variable Text" : "Segoe UI");
    font.setPointSizeF(9.5);
    QApplication::setFont(font);

    WheelGuard wheel_guard;
    app.installEventFilter(&wheel_guard);

    Backend backend;
    TH().set_mode(Theme::mode_from_string(backend.settings().theme));

    const QIcon icon = load_app_icon();
    if (!icon.isNull()) app.setWindowIcon(icon); /* before the window: the tray icon uses it */

    MainWindow window(&backend);
    window.winId(); /* a native window now, so the activation message finds it while hidden */
    ActivateFilter filter(&window);
    app.installNativeEventFilter(&filter);

    if (!start_minimized || !QSystemTrayIcon::isSystemTrayAvailable()) window.show();
    return app.exec();
}

} // namespace ui
