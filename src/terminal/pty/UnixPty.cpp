/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019-2020 Christian Parpart <christian@parpart.family>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <terminal/pty/UnixPty.h>

#include <crispy/deferred.h>
#include <crispy/escape.h>
#include <crispy/logstore.h>

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#if defined(__APPLE__)
    #include <util.h>
#elif defined(__FreeBSD__)
    #include <libutil.h>
    #include <termios.h>
#else
    #include <pty.h>
#endif

#include <fcntl.h>
#if !defined(__FreeBSD__)
    #include <utmp.h>
#endif
#include <pwd.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>

using std::max;
using std::min;
using std::nullopt;
using std::numeric_limits;
using std::optional;
using std::runtime_error;
using std::string_view;

using namespace std::string_literals;

namespace terminal
{

namespace
{
    static termios getTerminalSettings(int fd)
    {
        termios tio {};
        tcgetattr(fd, &tio);
        return tio;
    }

    static termios constructTerminalSettings(int fd)
    {
        auto tio = getTerminalSettings(fd);

        // input flags
#if defined(IUTF8)
        tio.c_iflag |=
            IUTF8; // Input is UTF-8; this allows character-erase to be properly applied in cooked mode.
#endif

        // special characters
        tio.c_cc[VMIN] = 1;  // Report as soon as 1 character is available.
        tio.c_cc[VTIME] = 0; // Disable timeout (no need).

        return tio;
    }

    bool setFileFlags(int fd, int flags) noexcept
    {
        int currentFlags {};
        if (fcntl(fd, F_GETFL, &currentFlags) < 0)
            return false;
        if (fcntl(fd, F_SETFL, currentFlags | flags) < 0)
            return false;
        return true;
    }

    void saveClose(int* fd)
    {
        if (fd && *fd != -1)
        {
            ::close(*fd);
            *fd = -1;
        }
    }

    UnixPty::PtyHandles createUnixPty(PageSize const& _windowSize, optional<ImageSize> _pixels)
    {
        // See https://code.woboq.org/userspace/glibc/login/forkpty.c.html
        assert(*_windowSize.lines <= numeric_limits<unsigned short>::max());
        assert(*_windowSize.columns <= numeric_limits<unsigned short>::max());

        winsize const ws { unbox<unsigned short>(_windowSize.lines),
                           unbox<unsigned short>(_windowSize.columns),
                           unbox<unsigned short>(_pixels.value_or(ImageSize {}).width),
                           unbox<unsigned short>(_pixels.value_or(ImageSize {}).height) };

#if defined(__APPLE__)
        winsize* wsa = const_cast<winsize*>(&ws);
#else
        winsize const* wsa = &ws;
#endif

        // TODO: termios term{};
        int masterFd {};
        int slaveFd {};
        if (openpty(&masterFd, &slaveFd, nullptr, /*&term*/ nullptr, (winsize*) wsa) < 0)
            throw runtime_error { "Failed to open PTY. "s + strerror(errno) };

        PtyLog()("PTY opened. master={}, slave={}", masterFd, slaveFd);

        return { PtyMasterHandle::cast_from(masterFd), PtySlaveHandle::cast_from(slaveFd) };
    }

} // namespace

// {{{ UnixPty::Slave
UnixPty::Slave::~Slave()
{
    close();
}

PtySlaveHandle UnixPty::Slave::handle() const noexcept
{
    return PtySlaveHandle::cast_from(_slaveFd);
}

void UnixPty::Slave::close()
{
    saveClose(&_slaveFd);
}

bool UnixPty::Slave::isClosed() const noexcept
{
    return _slaveFd == -1;
}

bool UnixPty::Slave::configure() noexcept
{
    auto const tio = constructTerminalSettings(_slaveFd);
    if (tcsetattr(_slaveFd, TCSANOW, &tio) == 0)
        tcflush(_slaveFd, TCIOFLUSH);
    return true;
}

bool UnixPty::Slave::login()
{
    if (_slaveFd < 0)
        return false;

    if (!configure())
        return false;

    return login_tty(_slaveFd) == 0;
}

int UnixPty::Slave::write(std::string_view text) noexcept
{
    if (_slaveFd < 0)
    {
        errno = ENODEV;
        return -1;
    }

    auto const rv = ::write(_slaveFd, text.data(), text.size());
    return static_cast<int>(rv);
}
// }}}

UnixPty::UnixPty(PageSize const& _windowSize, optional<ImageSize> _pixels):
    UnixPty(createUnixPty(_windowSize, _pixels), _windowSize)
{
}

UnixPty::UnixPty(PtyHandles handles, PageSize pageSize):
    _masterFd { unbox<int>(handles.master) },
    _buffer(4 * 1024 * 1024, {}),
    _pageSize { pageSize },
    _slave { handles.slave }
{
    if (!setFileFlags(_masterFd, O_CLOEXEC | O_NONBLOCK))
        throw runtime_error { "Failed to configure PTY. "s + strerror(errno) };

#if defined(__linux__)
    if (pipe2(_pipe.data(), O_NONBLOCK /* | O_CLOEXEC | O_NONBLOCK*/) < 0)
        throw runtime_error { "Failed to create PTY pipe. "s + strerror(errno) };
#else
    if (pipe(_pipe.data()) < 0)
        throw runtime_error { "Failed to create PTY pipe. "s + strerror(errno) };
    for (auto const fd: _pipe)
        if (!setFileFlags(fd, O_CLOEXEC | O_NONBLOCK))
            break;
#endif
}

UnixPty::~UnixPty()
{
    PtyLog()("PTY destroying master (file descriptor {}).", _masterFd);
    saveClose(&_pipe.at(0));
    saveClose(&_pipe.at(1));
    saveClose(&_masterFd);
}

PtySlave& UnixPty::slave() noexcept
{
    return _slave;
}

PtyMasterHandle UnixPty::handle() const noexcept
{
    return PtyMasterHandle::cast_from(_masterFd);
}

void UnixPty::close()
{
    PtyLog()("PTY closing master (file descriptor {}).", _masterFd);
    saveClose(&_masterFd);
    wakeupReader();
}

bool UnixPty::isClosed() const noexcept
{
    return _masterFd == -1;
}

void UnixPty::wakeupReader() noexcept
{
    // TODO(Linux): Using eventfd() instead could lower potential abuse.
    char dummy {};
    auto const rv = ::write(_pipe[1], &dummy, sizeof(dummy));
    (void) rv;
}

optional<string_view> UnixPty::readSome(char* target, size_t n) noexcept
{
    auto const rv = static_cast<int>(::read(_masterFd, target, n));
    if (rv < 0)
        return nullopt;

    if (PtyInLog)
        PtyInLog()("Received: {}", crispy::escape(target, target + rv));

    return string_view { target, static_cast<size_t>(rv) };
}

bool waitForReadable(int _masterFd, int* _pipe, std::chrono::milliseconds timeout) noexcept
{
    if (_masterFd < 0)
    {
        if (PtyInLog)
            PtyInLog()("read() called with closed PTY master.");
        errno = ENODEV;
        return false;
    }

    auto tv = timeval {};
    tv.tv_sec = timeout.count() / 1000;
    tv.tv_usec = static_cast<decltype(tv.tv_usec)>((timeout.count() % 1000) * 1000);

    for (;;)
    {
        fd_set rfd, wfd, efd;
        FD_ZERO(&rfd);
        FD_ZERO(&wfd);
        FD_ZERO(&efd);
        if (_masterFd != -1)
            FD_SET(_masterFd, &rfd);
        FD_SET(_pipe[0], &rfd);
        auto const nfds = 1 + max(_masterFd, _pipe[0]);

        int rv = select(nfds, &rfd, &wfd, &efd, &tv);
        if (rv == 0)
        {
            // (Let's not be too verbose here.)
            // PtyInLog()("PTY read() timed out.");
            errno = EAGAIN;
            return false;
        }

        if (_masterFd < 0)
        {
            errno = ENODEV;
            return false;
        }

        if (rv < 0)
        {
            PtyInLog()("PTY read() failed. {}", strerror(errno));
            return false;
        }

        bool piped = false;
        if (FD_ISSET(_pipe[0], &rfd))
        {
            piped = true;
            int n = 0;
            for (bool done = false; !done;)
            {
                char dummy[256];
                rv = static_cast<int>(::read(_pipe[0], dummy, sizeof(dummy)));
                done = rv > 0;
                n += max(rv, 0);
            }
        }

        if (FD_ISSET(_masterFd, &rfd))
            return true;

        if (piped)
        {
            errno = EINTR;
            return false;
        }
    }
}

optional<std::string_view> UnixPty::read(size_t size, std::chrono::milliseconds timeout)
{
    if (waitForReadable(_masterFd, _pipe.data(), timeout))
        return readSome(_buffer.data(), min(size, _buffer.size()));
    return nullopt;
}

int UnixPty::write(char const* buf, size_t size)
{
    timeval tv {};
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    fd_set rfd, wfd, efd;
    FD_ZERO(&rfd);
    FD_ZERO(&wfd);
    FD_ZERO(&efd);
    FD_SET(_masterFd, &wfd);
    FD_SET(_pipe[0], &rfd);
    auto const nfds = 1 + max(_masterFd, _pipe[0]);

    if (select(nfds, &rfd, &wfd, &efd, &tv) < 0)
        return -1;

    if (!FD_ISSET(_masterFd, &wfd))
    {
        PtyOutLog()("PTY write of {} bytes timed out.\n", size);
        return 0;
    }

    ssize_t rv = ::write(_masterFd, buf, size);
    if (PtyOutLog)
    {
        if (rv >= 0)
            PtyOutLog()("Sending bytes: \"{}\"", crispy::escape(buf, buf + rv));

        if (rv < 0)
            // errorlog()("PTY write failed: {}", strerror(errno));
            PtyOutLog()("PTY write of {} bytes failed. {}\n", size, strerror(errno));
        else if (0 <= rv && static_cast<size_t>(rv) < size)
            // clang-format off
            PtyOutLog()("Partial write. {} bytes written and {} bytes left.",
                        rv,
                        size - static_cast<size_t>(rv));
        // clang-format on
    }

    return static_cast<int>(rv);
}

PageSize UnixPty::pageSize() const noexcept
{
    return _pageSize;
}

void UnixPty::resizeScreen(PageSize cells, std::optional<ImageSize> pixels)
{
    if (_masterFd < 0)
        return;

    auto w = winsize {};
    w.ws_col = unbox<unsigned short>(cells.columns);
    w.ws_row = unbox<unsigned short>(cells.lines);

    if (pixels.has_value())
    {
        w.ws_xpixel = unbox<unsigned short>(pixels.value().width);
        w.ws_ypixel = unbox<unsigned short>(pixels.value().height);
    }

    if (ioctl(_masterFd, TIOCSWINSZ, &w) == -1)
        throw runtime_error { strerror(errno) };

    _pageSize = cells;
}

} // namespace terminal
