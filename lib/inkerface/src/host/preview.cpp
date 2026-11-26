// Implements a pixel-for-pixel Linux desktop view of the real framebuffer.
// X11 owns no application state; keyboard events become semantic actions.
// XWayland supports the same path on Wayland desktops.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih

#include "preview.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdexcept>
#include <utility>
#include <vector>

#include <poll.h>

namespace inkerface::detail {

namespace {

struct connection_closer {
    void operator()(Display *connection) const {
        XCloseDisplay(connection);
    }
};

using connection_owner = std::unique_ptr<Display, connection_closer>;

connection_owner open_connection() {
    connection_owner connection(XOpenDisplay(nullptr));
    if (!connection) {
        throw std::runtime_error(
            "cannot open X11 preview; check DISPLAY/XWayland, or unset "
            "INKERFACE_PREVIEW for headless use");
    }
    return connection;
}

// Window lifetime is nested within that of the connection.
class window_owner {
public:
    window_owner(Display &connection, size dimensions)
        : connection_(&connection)
        , id_(XCreateSimpleWindow(
              connection_,
              DefaultRootWindow(connection_),
              0,
              0,
              static_cast<unsigned int>(dimensions.width),
              static_cast<unsigned int>(dimensions.height),
              0,
              BlackPixel(connection_, DefaultScreen(connection_)),
              WhitePixel(connection_, DefaultScreen(connection_)))) {
        if (id_ == 0) {
            throw std::runtime_error("cannot create preview window");
        }
    }

    ~window_owner() {
        XDestroyWindow(connection_, id_);
    }
    window_owner(const window_owner &) = delete;
    window_owner &operator=(const window_owner &) = delete;
    window_owner(window_owner &&) = delete;
    window_owner &operator=(window_owner &&) = delete;

    [[nodiscard]] Window id() const {
        return id_;
    }

private:
    Display *connection_;
    Window id_;
};

} // namespace

class preview_backend {
public:
    explicit preview_backend(display &out)
        : out_(&out)
        , connection_(open_connection())
        , window_(*connection_, out.frame().extent()) {
        Display *connection = connection_.get();
        const size dimensions = out.frame().extent();
        XSizeHints hints{};
        hints.flags = PMinSize | PMaxSize;
        hints.min_width = hints.max_width = dimensions.width;
        hints.min_height = hints.max_height = dimensions.height;
        XSetWMNormalHints(connection, window_.id(), &hints);
        XStoreName(
            connection, window_.id(), "inkerface - X4 preview (480 x 800)");
        XSelectInput(connection, window_.id(), ExposureMask | KeyPressMask);
        close_message_ = XInternAtom(connection, "WM_DELETE_WINDOW", False);
        protocols_message_ = XInternAtom(connection, "WM_PROTOCOLS", False);
        XSetWMProtocols(connection, window_.id(), &close_message_, 1);
        XMapWindow(connection, window_.id());
    }

    input_snapshot poll(int timeout_ms) {
        input_snapshot snapshot;
        if (closed_) {
            return snapshot;
        }
        refresh();
        Display *connection = connection_.get();
        if (timeout_ms >= 0 && XPending(connection) == 0) {
            pollfd watched{ConnectionNumber(connection), POLLIN, 0};
            if (::poll(&watched, 1, timeout_ms) <= 0 ||
                XPending(connection) == 0) {
                return snapshot; // nothing arrived in time
            }
        }
        XEvent event{};
        XNextEvent(connection, &event);
        if (event.type == Expose) {
            painted_.clear(); // uncovered: repaint on the next call
        } else if (event.type == ClientMessage &&
            event.xclient.message_type == protocols_message_ &&
            event.xclient.format == 32 &&
            static_cast<Atom>(event.xclient.data.l[0]) == close_message_) {
            closed_ = true;
        } else if (event.type == KeyPress) {
            const KeySym key = XLookupKeysym(&event.xkey, 0);
            snapshot.confirm = key == XK_Return || key == XK_space;
            snapshot.back = key == XK_Escape || key == XK_BackSpace;
            snapshot.focus_next = key == XK_Down || key == XK_Tab;
            snapshot.focus_prev = key == XK_Up;
            snapshot.page_next = key == XK_Right || key == XK_Page_Down;
            snapshot.page_prev = key == XK_Left || key == XK_Page_Up;
            // The keyboard has no power button; P stands in for holding it.
            snapshot.power = key == XK_p;
        }
        return snapshot;
    }

    [[nodiscard]] bool closed() const {
        return closed_;
    }

private:
    // Repaint the window only when the frame differs from what it shows;
    // a loop polling several times a second then costs almost nothing.
    void refresh() {
        const surface frame = out_->frame();
        const size dimensions = frame.extent();
        std::vector<bool> ink;
        ink.reserve(static_cast<std::size_t>(dimensions.width) *
                    static_cast<std::size_t>(dimensions.height));
        for (coord y = 0; y < dimensions.height; ++y) {
            for (coord x = 0; x < dimensions.width; ++x) {
                ink.push_back(frame.pixel_at({x, y}) == color::black);
            }
        }
        if (ink != painted_) {
            painted_ = std::move(ink);
            paint();
        }
    }

    void paint() {
        const surface frame = out_->frame();
        const size dimensions = frame.extent();
        Display *connection = connection_.get();
        const int screen = DefaultScreen(connection);
        // The default GC belongs to the connection; it is never freed here.
        const GC graphics = DefaultGC(connection, screen);
        XSetForeground(connection, graphics, WhitePixel(connection, screen));
        XFillRectangle(connection,
                       window_.id(),
                       graphics,
                       0,
                       0,
                       static_cast<unsigned int>(dimensions.width),
                       static_cast<unsigned int>(dimensions.height));
        XSetForeground(connection, graphics, BlackPixel(connection, screen));
        for (coord y = 0; y < dimensions.height; ++y) {
            coord x = 0;
            while (x < dimensions.width) {
                if (frame.pixel_at({x, y}) != color::black) {
                    ++x;
                    continue;
                }
                const coord start = x;
                while (x < dimensions.width &&
                       frame.pixel_at({x, y}) == color::black) {
                    ++x;
                }
                XFillRectangle(connection,
                               window_.id(),
                               graphics,
                               start,
                               y,
                               static_cast<unsigned int>(x - start),
                               1);
            }
        }
        XFlush(connection);
    }

    display *out_;
    connection_owner connection_;
    window_owner window_;
    Atom close_message_ = 0;
    Atom protocols_message_ = 0;
    bool closed_ = false;
    // What the window shows, one entry per pixel; empty forces a repaint.
    std::vector<bool> painted_;
};

preview_window::preview_window(display &out)
    : backend_(std::make_unique<preview_backend>(out)) {}

preview_window::~preview_window() = default;

input_snapshot preview_window::poll(int timeout_ms) {
    return backend_->poll(timeout_ms);
}

bool preview_window::closed() const {
    return backend_->closed();
}

} // namespace inkerface::detail
