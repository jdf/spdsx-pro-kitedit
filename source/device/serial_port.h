// The byte channel to the device's USB CDC-ACM node.
//
// SerialPort is the interface SpdsxDevice talks through — the whole surface
// is Write and ReadExact — so a test can stand a fake in place of hardware.
// MacOSSerialPort is the real one, and the only one: macOS is where this is
// implemented and tested.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_SERIAL_PORT_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_SERIAL_PORT_H_

#include <cstddef>
#include <string>
#include <vector>

#include "device/protocol.h"  // Bytes

namespace spdsx::device {

// Ports matching /dev/cu.usbmodem*, sorted. The device gets a fresh node
// number every time it's plugged in, so there is no stable path to default
// to — enumerate and ping instead.
std::vector<std::string> ListUsbModemPorts();

// An open byte channel. SpdsxDevice borrows one and does not own it.
class SerialPort {
public:
  virtual ~SerialPort() = default;

  virtual void Write(const Bytes& data) = 0;
  // Reads up to n bytes, returning early if timeout_seconds elapses first.
  virtual Bytes ReadExact(size_t n, double timeout_seconds) = 0;
};

// The real port. macOS-specific: the SPD-SX PRO runs at a non-standard 230400
// baud, set via the IOSSIOSPEED ioctl exactly as the official app does.
// Ported from the fd handling in spdsx-py's SPDSX class.
class MacOSSerialPort : public SerialPort {
public:
  // Opens the port and configures raw mode + baud. Throws std::runtime_error
  // if the node can't be opened.
  explicit MacOSSerialPort(const std::string& path, int baud = 230400);
  ~MacOSSerialPort() override;
  MacOSSerialPort(const MacOSSerialPort&) = delete;
  MacOSSerialPort& operator=(const MacOSSerialPort&) = delete;

  void Write(const Bytes& data) override;
  Bytes ReadExact(size_t n, double timeout_seconds) override;

private:
  int fd_ = -1;
};

}  // namespace spdsx::device

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_SERIAL_PORT_H_
