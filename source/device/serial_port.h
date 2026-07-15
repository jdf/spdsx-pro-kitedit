// Raw serial connection to the device's USB CDC-ACM node.
//
// macOS-specific: the SPD-SX PRO runs at a non-standard 230400 baud, set via
// the IOSSIOSPEED ioctl exactly as the official app does. Ported from the fd
// handling in spdsx-py's SPDSX class.
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

class SerialPort {
public:
  // Opens the port and configures raw mode + baud. Throws std::runtime_error
  // if the node can't be opened.
  explicit SerialPort(const std::string& path, int baud = 230400);
  ~SerialPort();
  SerialPort(const SerialPort&) = delete;
  SerialPort& operator=(const SerialPort&) = delete;

  void Write(const Bytes& data);
  // Reads up to n bytes, returning early if timeout_seconds elapses first.
  Bytes ReadExact(size_t n, double timeout_seconds);

private:
  int fd_ = -1;
};

}  // namespace spdsx::device

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_SERIAL_PORT_H_
