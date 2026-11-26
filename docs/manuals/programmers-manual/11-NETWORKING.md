# 11. Networking

A reader fetches catalogs, syncs reading positions and downloads books.
In this chapter you join Wi-Fi, call a web API, stream a download onto
the SD card, and talk to a server over a plain TCP socket. Then you turn
it around: accept connections, find other devices by broadcast, and
become an access point, which is how a device with no keyboard is set
up from a phone.

## The classes

| Header | Class | Job |
| --- | --- | --- |
| `<inkerface/network.h>` | `network` | the link: joins Wi-Fi on the X4, scans, hosts an access point, switches the radio off |
| `<inkerface/http.h>` | `http_client` | HTTP and HTTPS requests to web APIs |
| `<inkerface/socket.h>` | `tcp_socket` | a TCP connection, optionally with TLS, for other protocols |
| `<inkerface/socket.h>` | `tcp_listener` | accepts TCP connections from other devices |
| `<inkerface/socket.h>` | `udp_socket` | datagrams, and broadcasts to the whole local network |

A program creates **one** `network`. Sockets and clients are made *on*
it, and fail with `net_error::not_connected` while it is down. A
listener and a UDP socket also work while the network is *hosting*, that
is, while the device is an access point of its own.

Underneath, inkerface uses FreeInk's network code on both targets:
`SecureHttpClient` for HTTP (framing, redirects, chunked bodies,
connection reuse) and `SecureClient` for TLS. On the desktop, a small
Arduino stand-in connects them to the operating system's sockets and to
OpenSSL, so the HTTP code you test on the desktop is the HTTP code that
runs on the device.

## The link

```cpp
inkerface::network link;
if (!link.connect("my-wifi", "secret")) {
    // not joined within the timeout (15 seconds by default)
}
// ... use the network ...
link.disconnect();   // radio off
```

- On the **X4**, `connect()` joins the Wi-Fi network and waits for an
  address. The credentials are *not* saved to flash. `disconnect()`
  switches the radio off.
- On the **desktop**, the operating system's network is used.
  `connect()` succeeds immediately and ignores the credentials, and
  `disconnect()` only marks the link down, so you can test your
  "offline" paths.

**Power:** the radio is the most power-hungry part of the device after
the panel refresh. Connect when you need the network, do the work, and
disconnect. Don't keep Wi-Fi up while the reader is just showing a page.

## Calling a web API

```cpp
inkerface::http_client client(link);
const inkerface::http_response answer =
    client.send({.url = "http://example.com/api/books"});
if (answer.ok()) {
    // answer.status == 200, answer.body holds the response
}
```

A request is an `http_request`. Set only what you need, using C++20
designated initializers:

| Field | Default | Meaning |
| --- | --- | --- |
| `method` | `get` | `get`, `post`, `put`, `remove` (DELETE) |
| `url` | | `http://` or `https://` URL |
| `headers` | none | extra header lines, e.g. `{"Accept", "application/json"}` |
| `body` | empty | sent with `Content-Length` when not empty |
| `timeout` | 15 s | to connect, and between received bytes |
| `max_body_bytes` | 64 KB | largest body collected into `response.body` |
| `max_redirects` | 3 | redirects followed automatically |
| `ca_certificate` | none | PEM root certificate for HTTPS (see below) |
| `insecure` | false | skip certificate checks: **testing only** |

```cpp
const inkerface::http_response created = client.send({
    .method = inkerface::http_method::post,
    .url = "http://example.com/api/notes",
    .headers = {{"Content-Type", "application/json"}},
    .body = R"({"page": 42})",
});
```

### The response

| Member | Meaning |
| --- | --- |
| `error` | `net_error::none` if a response arrived, whatever its status |
| `status` | the HTTP status (200, 404, ...); 0 if nothing arrived |
| `body` | the body, unless it was streamed |
| `headers` | the response headers, with lower-case names |
| `ok()` | true for a complete 2xx response |
| `header(name)` | one header's value, with the name compared without case |

A **404 is not a network error**. `error` is `none` and `status` says
404. Check `ok()` when you need success, and look at `error` and
`status` separately when you need to know *why* not.

| `net_error` | Meaning |
| --- | --- |
| `not_connected` | the link is down |
| `invalid_address` | malformed URL, empty host, port 0, or a header containing a line break |
| `resolve` | the host name was not found (sockets) |
| `connect` | the server could not be reached, refused, failed TLS, or the connection broke |
| `tls_unavailable` | HTTPS or TLS was requested, but this build has no TLS (see below) |
| `closed` | the peer closed the connection (sockets) |
| `cancelled` | your cancel check or body sink stopped the transfer |
| `too_large` | the body exceeded `max_body_bytes` |
| `incomplete` | the body ended early |
| `bind` | a listener or UDP socket could not take its local port: it is in use, or not permitted (below 1024 on the desktop) |

### Streaming large bodies

`response.body` holds the whole body in RAM, so keep `max_body_bytes`
small. For downloads, pass a **sink**: a function that receives the body
in chunks as they arrive and returns `false` to stop. Writing the chunks
to a file keeps RAM use at one chunk, however large the download:

```cpp
inkerface::file out(disks, inkerface::volume::card, "books/a.epub",
                    inkerface::file_mode::write);
const inkerface::http_response result = client.send(
    {.url = "http://example.com/a.epub"},
    [&out](std::span<const std::byte> chunk) {
        return out.write(chunk) == chunk.size();   // false stops the download
    });
const bool saved = result.ok() && out.close();
```

### Cancelling

Requests block until they finish. To stop one early, pass a cancel
check. It is called repeatedly while the request runs, and must return
quickly:

```cpp
using clock = std::chrono::steady_clock;
const auto give_up = clock::now() + std::chrono::seconds(30);
const auto answer = client.send(request, [give_up]() {
    return clock::now() > give_up;
});
```

Don't call `input::poll()` from a cancel check: on the desktop it waits
for the next key or script line.

### Connection reuse

`http_client` keeps the connection to the last server open, so a series
of requests to one server pays for one connection, and for HTTPS one TLS
handshake, which is slow and memory-hungry on the device. Call
`client.close()` when you are done with a server. Destroying the client
closes it too.

## HTTPS and certificates

HTTPS needs a TLS implementation:

| Target | TLS | Certificate check |
| --- | --- | --- |
| Desktop | OpenSSL (`make` builds it in; `cmake -DINKERFACE_TLS=OFF` leaves it out) | the system's certificates, or `ca_certificate` |
| X4 | FreeInk's wolfSSL client, **off by default** | only `ca_certificate`; the device has no certificate store |

**On the X4, TLS is off unless you enable it,** and HTTPS requests return
`tls_unavailable`. FreeInk's TLS client needs the wolfSSL library, which
is licensed under the GPL version 2 or a commercial licence. Check that
this suits how you distribute your firmware before enabling it. To enable
it, add `-DFREEINK_NET_WOLFSSL=1` to `build_flags` in `platformio.ini`,
add wolfSSL to `lib_deps`, and provide its `user_settings.h`, as
described in FreeInk's SecureNet library.

With TLS on, a request is accepted only if the server's certificate chain
leads to the root you pass in `ca_certificate`, a PEM string:

```cpp
constexpr const char *my_root =
    "-----BEGIN CERTIFICATE-----\n"
    "...\n"
    "-----END CERTIFICATE-----\n";
client.send({.url = "https://api.example.com/", .ca_certificate = my_root});
```

`insecure = true` turns the check off. Then anyone between the device and
the server can read and change the traffic. Use it only to test against
your own server.

## Sockets

For protocols other than HTTP, use `tcp_socket`:

```cpp
inkerface::tcp_socket line(link, "192.168.1.10", 7000);
if (!line) {
    // line.error(): resolve, connect, tls_unavailable, ...
}
static_cast<void>(line.write("HELLO\n"));
std::array<std::byte, 256> buffer{};
const std::size_t got = line.read(buffer);   // waits up to the timeout
```

- `read()` waits up to the socket's timeout (10 seconds by default) for
  data, then returns what has arrived, up to the buffer size. It returns
  0 in three cases, which `error()` tells apart:
  - `net_error::none`: the timeout passed. The socket is still open, so
    try again later.
  - `net_error::closed`: the peer closed the connection.
  - another error: the connection failed.
- `read(buffer, wait)` waits at most `wait` instead. With `0ms` it never
  waits: it returns what has arrived, or 0, and still notices a closed
  connection. Use it to poll a socket from your main loop (chapter 13).
- `available()` says how many bytes can be read without waiting.
- Failures are sticky and close the socket, as with files.
- TLS: set `socket_options{.tls = true, .ca_certificate = ...}`. The same
  rules as for HTTPS apply.

## Serving and finding devices

### Accepting connections

`tcp_listener` listens on a port on every interface and hands each
connection over as an ordinary `tcp_socket`:

```cpp
inkerface::tcp_listener server(link, 8080);
if (!server) {
    // server.error(): not_connected, or bind when the port is taken
}
inkerface::tcp_socket peer = server.accept();   // 0ms: never waits
if (peer) {
    // read and write it like any other tcp_socket
}
```

`accept()` returns a closed socket (`is_open()` false, `error()` none)
when nobody is waiting, so a main loop can call it on every pass.
`accept(wait)` waits at most `wait`. Accepted connections are plain
TCP; TLS is for outgoing connections only. On the desktop, ports below
1024 need root, and asking for one is reported as `net_error::bind`.

### Datagrams and broadcast

`udp_socket` sends and receives single datagrams. `broadcast()` sends
one to every device on the local network, which is how a device finds a
server it was never told the address of:

```cpp
inkerface::udp_socket asker(link);              // any free port
static_cast<void>(asker.broadcast(7001, std::as_bytes(std::span("who?", 4))));

std::array<std::byte, 512> buffer{};
inkerface::datagram_source from;
const std::size_t got = asker.receive(buffer, from, 100ms);
if (got > 0) {
    // from.address and from.port say who answered
}
```

- `receive()` returns one datagram, or 0 when none arrived within the
  wait. With `0ms` it never waits, for polling from a main loop.
- A datagram can be lost; ask again if no answer comes.
- A failed send is reported by its return value and leaves the socket
  open: the next one may well go through.
- Where a broadcast goes is the platform's choice. On the X4 it is the
  broadcast address of the joined network, or of the access point. On
  the desktop it is every interface's broadcast address and loopback, so
  a program on the same machine hears it too.

### Becoming an access point

A device with no keyboard cannot type a Wi-Fi password, so it becomes a
network of its own, a phone joins it, and a page served by the device
asks for the password:

```cpp
const auto networks = link.scan();       // strongest first; a few seconds
const auto hosted = link.start_access_point("reader_1234", "k7mq4xvp");
if (hosted) {
    // hosted->address is where phones reach the device, "192.168.4.1"
    inkerface::tcp_listener page(link, hosted->emulated ? 8080 : 80);
    // ... serve a form, read the network and password from it ...
}
link.stop_access_point();
static_cast<void>(link.connect(chosen_ssid, chosen_password));
```

- The password must be 8 to 63 characters (WPA2), or empty for an open
  network. Show it on the panel; whoever can see the screen can join.
- `connect()` ends hosting. So does `disconnect()`.
- `scan()` keeps the access point up while it listens on every channel.
- On the **desktop** there is no radio. `scan()` returns a fixed list,
  and the access point is *emulated*: `hosted->emulated` is true and
  `hosted->address` is the machine's own LAN address, so a real phone on
  the same network can open your setup page. Use an unprivileged port
  there.
- A phone that finds no internet looks for a captive portal. Answering
  DNS on UDP port 53 with the device's own address for every name, and
  redirecting every unknown page to your form, makes the phone open the
  form by itself.

## Example: fetch and save

This program reads Wi-Fi credentials from `wifi.txt` on internal flash
(the network name on the first line, the password on the second), fetches
a page, saves a copy on the card, and shows what happened. Replace
`src/app/main.cpp`:

File: `src/app/main.cpp`

```cpp
//
// Joins Wi-Fi with credentials from internal storage, fetches a page,
// saves a copy on the card, and shows the result.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <inkerface/color.h>
#include <inkerface/display.h>
#include <inkerface/file.h>
#include <inkerface/font.h>
#include <inkerface/geometry.h>
#include <inkerface/http.h>
#include <inkerface/input.h>
#include <inkerface/network.h>
#include <inkerface/program.h>
#include <inkerface/storage.h>
#include <inkerface/surface.h>

#include <array>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using inkerface::file;
using inkerface::file_mode;
using inkerface::storage;
using inkerface::volume;

constexpr const char *page_url = "http://example.com/";

struct wifi_credentials {
    std::string name;
    std::string password;
};

// Read "name\npassword" from wifi.txt on internal flash.
std::optional<wifi_credentials> read_credentials(storage &disks) {
    file in(disks, volume::internal, "wifi.txt", file_mode::read);
    std::array<char, 160> text{};
    const std::size_t length =
        in.read(std::as_writable_bytes(std::span(text)));
    const std::string content(text.data(), length);
    const std::size_t newline = content.find('\n');
    if (!in || newline == std::string::npos) {
        return std::nullopt;
    }
    std::string password = content.substr(newline + 1);
    while (!password.empty() &&
           (password.back() == '\n' || password.back() == '\r')) {
        password.pop_back();
    }
    return wifi_credentials{content.substr(0, newline), password};
}

// Fetch the page into downloads/example.html on the card.
std::string fetch(inkerface::network &link, storage &disks) {
    if (!disks.available(volume::card)) {
        return "No card to save to";
    }
    static_cast<void>(disks.make_directory(volume::card, "downloads"));
    file out(disks, volume::card, "downloads/example.html", file_mode::write);
    inkerface::http_client client(link);
    const inkerface::http_response answer = client.send(
        {.url = page_url}, [&out](std::span<const std::byte> chunk) {
            return out.write(chunk) == chunk.size();
        });
    const bool saved = out.close();
    if (answer.error != inkerface::net_error::none) {
        return "Request failed";
    }
    if (!answer.ok() || !saved) {
        return "Status " + std::to_string(answer.status) + ", not saved";
    }
    return "Saved " + std::to_string(out.size()) + " bytes (" +
           std::string(answer.header("content-type")) + ")";
}

void show(inkerface::surface &frame, const std::vector<std::string> &lines) {
    const inkerface::size whole = frame.extent();
    const inkerface::coord line = frame.line_height(inkerface::font::regular());
    inkerface::text_style style;
    style.max_lines = 2;
    frame.clear(inkerface::color::white);
    inkerface::coord y = 40;
    for (const std::string &text : lines) {
        frame.draw_text(inkerface::rect{24,
                                        y,
                                        static_cast<inkerface::coord>(
                                            whole.width - 48),
                                        static_cast<inkerface::coord>(
                                            2 * line)},
                        text,
                        style);
        y = static_cast<inkerface::coord>(y + 2 * line);
    }
}

} // namespace

int program(int /*argc*/, char ** /*argv*/) {
    inkerface::display screen;
    inkerface::input buttons(screen);
    storage disks;
    inkerface::network link;

    std::vector<std::string> lines{page_url};
    const std::optional<wifi_credentials> wifi = read_credentials(disks);
    if (!wifi) {
        lines.emplace_back("Put the Wi-Fi name and password in wifi.txt");
    } else if (!link.connect(wifi->name, wifi->password)) {
        lines.emplace_back("Could not join " + wifi->name);
    } else {
        lines.push_back(fetch(link, disks));
        link.disconnect(); // done: switch the radio off
    }

    inkerface::surface frame = screen.frame();
    show(frame, lines);
    if (!screen.present(inkerface::refresh_mode::full)) {
        return EXIT_FAILURE;
    }
    while (!buttons.closed()) {
        static_cast<void>(buttons.poll());
    }
    return EXIT_SUCCESS;
}
```

On the desktop, create the credentials file once (the content doesn't
matter there) and run it:

```sh
make storage-images
printf 'my-wifi\nsecret\n' > build/wifi.txt
INKERFACE_STORAGE_ROOT="$PWD/build/storage" \
    bin/debug/inkerface_storage put internal build/wifi.txt wifi.txt
make run
```

The page shows the saved size and content type, and the copy is in
`downloads/example.html` inside `build/storage/card.fat`. Export it with
the storage utility from chapter 9. Without internet access, it
shows "Request failed".

On the X4, put your real credentials into `wifi.txt` on the internal
volume, for example with `pio run -e x4 -t uploadfs` from a `data/`
folder (chapter 10). Keep that file out of version control.

## On the device

| | Cost |
| --- | --- |
| Flash | networking is linked only into programs that use it |
| RAM while connected | the Wi-Fi stack's buffers, plus a TLS session if TLS is on (chapter 8 has the budget) |
| Power | the radio dominates; `disconnect()` as soon as you are done, and `stop_access_point()` once setup is over |
| Scan | blocks for a few seconds while the radio listens on every channel |

Requests and socket reads block the thread that makes them. Either show a
"Loading..." page first (paint and present it, then send the request), or
make requests from a worker thread so the UI stays responsive: chapter 12
shows how.

## Testing network code

`tests/unit/test_network.cpp` runs a small scripted server on
`127.0.0.1` in the test process. Each test connection gets a prepared
answer, and the test checks what the client sent. This tests the real
client code without internet access. Copy that pattern to test your own
protocol code. `tests/unit/test_network_server.cpp` does the same for
the server side: a listener and a client in one process, datagrams and a
broadcast over loopback, and the emulated access point.

## Summary

- One `network`: connect, work, disconnect (the radio is expensive).
- `http_client::send()` returns a response; `ok()` means a complete 2xx.
  A 404 is a response, not an error.
- Stream large bodies to a file with a sink; keep `max_body_bytes` small.
- HTTPS works on the desktop. On the X4 it needs wolfSSL to be enabled
  and a CA certificate, so check the licence first.
- `tcp_socket` covers other protocols; a read that times out is not an
  error.
- `tcp_listener` accepts connections; `udp_socket` sends datagrams and
  broadcasts; both work on a joined network and on the device's own
  access point.
- `start_access_point()` and `scan()` let a phone set the device up. On
  the desktop both are emulated.

Next: [Multithreading](12-MULTITHREADING.md).
