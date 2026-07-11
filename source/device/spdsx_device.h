// High-level connection to the SPD-SX PRO: the transport frame plus the
// device operations. Mirrors spdsx-py's SPDSX class.
//
// Every payload goes on the wire wrapped in the app's transport frame:
//   0d 60 e0 <ch> <8 junk bytes> 01 00 00 00 <len LE32> <payload>
// ch 0x07 = DT1 parameter writes, 0x09 = control/status. Focus/select
// commands reply and must be drained; parameter writes don't ack.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_SPDSX_DEVICE_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_SPDSX_DEVICE_H_

#include <string>

#include "device/protocol.h"
#include "device/serial_port.h"

namespace spdsx::device {

// Wraps a payload in the transport frame; the channel is chosen from the
// payload's message family.
Bytes Wrap(const Bytes& payload);
// Extracts the payload from a framed reply; empty if malformed.
Bytes Unwrap(const Bytes& frame);

class SpdsxDevice {
 public:
  explicit SpdsxDevice(const std::string& port);

  // Writes a framed payload and waits for the device's framed reply. Use for
  // messages that reply (kit-select, object-focus, reads).
  Bytes Command(const Bytes& payload, double timeout_seconds = 0.4);
  // Fire-and-forget: frame and write, no reply expected (parameter writes).
  void Send(const Bytes& payload);
  // Reads exactly one framed reply and returns its payload.
  Bytes ReadFrame(double timeout_seconds = 0.4);

  Bytes Ping();
  Bytes SelectKit(int kit);
  Bytes SelectObject(ObjectKind kind, int index);
  // Focus (replies; drains) then write the pad-link group (fire-and-forget)
  // for a specific kit; pace_seconds paces the no-ack write.
  void SetPadLink(int kit, ObjectKind kind, int index, int group,
      double pace_seconds = 0.02);

 private:
  SerialPort port_;
};

}  // namespace spdsx::device

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_SPDSX_DEVICE_H_
