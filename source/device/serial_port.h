// The byte channel to the device, and the platform's supply of them.
//
// Both are interfaces, and this header names no platform: SerialPort is what
// SpdsxDevice talks through (the whole surface is Write and ReadExact), and
// PortBackend is where ports come from — which nodes might be the device, and
// how one is opened. Exactly one implementation of PortBackend is linked; the
// build picks it (see serial_port_macos.cc, and CMakeLists.txt, which fails
// at configure time on a platform that has none).
//
// Porting means implementing these two, not editing anything that uses them.
// Faking them is what lets the device layer be tested without hardware.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_SERIAL_PORT_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_SERIAL_PORT_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "device/protocol.h"  // Bytes

namespace spdsx::device {

// An open byte channel. SpdsxDevice borrows one and does not own it.
class SerialPort {
public:
  virtual ~SerialPort() = default;

  virtual void Write(const Bytes& data) = 0;
  // Reads up to n bytes, returning early if timeout_seconds elapses first.
  virtual Bytes ReadExact(size_t n, double timeout_seconds) = 0;
};

// Where ports come from. The two things that are actually platform-specific
// about finding the device, behind one seam.
class PortBackend {
public:
  virtual ~PortBackend() = default;

  // Every node that might be the device, in a stable order. The device gets a
  // fresh one each replug, so there is no stable path to default to —
  // enumerate and ping instead.
  virtual std::vector<std::string> ListCandidates() = 0;

  // Opens a candidate. Throws std::runtime_error if it can't be opened (busy,
  // or gone since it was listed).
  virtual std::unique_ptr<SerialPort> Open(const std::string& path) = 0;
};

// The backend this build linked.
PortBackend& PlatformPorts();

}  // namespace spdsx::device

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_SERIAL_PORT_H_
