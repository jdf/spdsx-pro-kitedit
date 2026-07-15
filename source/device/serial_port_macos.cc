// The macOS PortBackend: the whole of what is platform-specific about
// reaching the device. The IOSSIOSPEED ioctl for its non-standard 230400
// baud, and the /dev/cu.usbmodem* node naming. Ported from the fd handling
// in spdsx-py's SPDSX class.
//
// CMakeLists.txt only compiles this on APPLE and fails at configure time
// elsewhere, so this guard is belt-and-braces — but a source list can always
// be edited by hand, and a missing-IOKit error two lines down explains
// nothing.
#if !defined(__APPLE__)
#  error \
      "serial_port_macos.cc is the macOS PortBackend; the build should not be compiling it on this platform. Implement PortBackend + SerialPort instead."
#endif

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <stdexcept>

#include <IOKit/serial/ioss.h>  // IOSSIOSPEED
#include <fcntl.h>
#include <glob.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "device/serial_port.h"

namespace spdsx::device {

namespace {

double NowSeconds() {
  timespec ts {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

// Ports matching /dev/cu.usbmodem*, sorted. The device gets a fresh node
// number on every replug, so there is no stable path to default to.
std::vector<std::string> ListUsbModemPorts() {
  std::vector<std::string> ports;
  glob_t g {};
  if (::glob("/dev/cu.usbmodem*", 0, nullptr, &g) == 0) {
    for (size_t i = 0; i < g.gl_pathc; ++i) {
      ports.emplace_back(g.gl_pathv[i]);
    }
  }
  ::globfree(&g);
  return ports;
}

// Raw mode at the device's non-standard baud, over the CDC-ACM node.
class MacOSSerialPort : public SerialPort {
public:
  explicit MacOSSerialPort(const std::string& path, int baud = 230400);
  ~MacOSSerialPort() override;
  MacOSSerialPort(const MacOSSerialPort&) = delete;
  MacOSSerialPort& operator=(const MacOSSerialPort&) = delete;

  void Write(const Bytes& data) override;
  Bytes ReadExact(size_t n, double timeout_seconds) override;

private:
  int fd_ = -1;
};

MacOSSerialPort::MacOSSerialPort(const std::string& path, int baud) {
  fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) {
    throw std::runtime_error("cannot open " + path + ": "
                             + std::strerror(errno));
  }
  // Opened non-blocking to avoid hanging on modem control lines; switch to
  // blocking now for straightforward read/write.
  const int flags = ::fcntl(fd_, F_GETFL);
  ::fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);

  termios a {};
  if (::tcgetattr(fd_, &a) != 0) {
    ::close(fd_);
    fd_ = -1;
    throw std::runtime_error("tcgetattr failed on " + path);
  }
  // Raw mode: no input/output/line processing.
  a.c_iflag = 0;
  a.c_oflag = 0;
  a.c_lflag = 0;
  a.c_cflag |= (CLOCAL | CREAD);
  a.c_cflag &= ~CRTSCTS;
  ::tcsetattr(fd_, TCSANOW, &a);

  speed_t speed = static_cast<speed_t>(baud);
  if (::ioctl(fd_, IOSSIOSPEED, &speed) != 0) {
    // Non-fatal, mirroring the Python: some adapters still work.
    std::fprintf(
        stderr, "warning: could not set baud: %s\n", std::strerror(errno));
  }
  ::tcflush(fd_, TCIOFLUSH);
}

MacOSSerialPort::~MacOSSerialPort() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

void MacOSSerialPort::Write(const Bytes& data) {
  size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t n = ::write(fd_, data.data() + offset, data.size() - offset);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error(std::string("serial write failed: ")
                               + std::strerror(errno));
    }
    offset += static_cast<size_t>(n);
  }
}

Bytes MacOSSerialPort::ReadExact(size_t n, double timeout_seconds) {
  Bytes buf;
  buf.reserve(n);
  const double deadline = NowSeconds() + timeout_seconds;
  while (buf.size() < n) {
    const double remaining = deadline - NowSeconds();
    if (remaining <= 0) {
      break;
    }
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd_, &rfds);
    timeval tv;
    tv.tv_sec = static_cast<time_t>(remaining);
    tv.tv_usec = static_cast<suseconds_t>((remaining - tv.tv_sec) * 1e6);
    const int r = ::select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
    if (r <= 0) {
      break;  // timeout or error
    }
    uint8_t chunk[256];
    size_t want = n - buf.size();
    if (want > sizeof(chunk)) {
      want = sizeof(chunk);
    }
    const ssize_t got = ::read(fd_, chunk, want);
    if (got <= 0) {
      break;
    }
    buf.insert(buf.end(), chunk, chunk + got);
  }
  return buf;
}

class MacOSPortBackend : public PortBackend {
public:
  std::vector<std::string> ListCandidates() override {
    return ListUsbModemPorts();
  }

  std::unique_ptr<SerialPort> Open(const std::string& path) override {
    return std::make_unique<MacOSSerialPort>(path);
  }
};

}  // namespace

PortBackend& PlatformPorts() {
  static MacOSPortBackend backend;
  return backend;
}

}  // namespace spdsx::device
