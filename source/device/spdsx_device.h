// High-level connection to the SPD-SX PRO: the transport frame plus the
// device operations. Mirrors spdsx-py's SPDSX class.
//
// Every payload goes on the wire wrapped in the app's transport frame:
//   0d 60 e0 <ch> <8 junk bytes> 01 00 00 00 <len LE32> <payload>
// ch 0x07 = DT1 parameter writes, 0x09 = control/status. Focus/select
// commands reply and must be drained; parameter writes don't ack.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_SPDSX_DEVICE_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_SPDSX_DEVICE_H_

#include <functional>
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
  // Control/status query on the 41 6a family. NOTE: the category byte's
  // position is extrapolated from the ping message, not yet verified
  // against captures — treat replies with suspicion until confirmed
  // live (category 0x17 is believed to return the firmware version).
  Bytes StatusRequest(uint8_t category);
  Bytes SelectKit(int kit);
  Bytes SelectObject(ObjectKind kind, int index);
  // Focus (replies; drains) then write the pad-link group (fire-and-forget)
  // for a specific kit; pace_seconds paces the no-ack write.
  void SetPadLink(int kit, ObjectKind kind, int index, int group,
      double pace_seconds = 0.02);

  // Selects the kit, then writes its 16-char name (space-padded/truncated).
  void SetKitName(int kit, const std::string& name, double pace_seconds = 0.02);

  // Selects the kit, focuses the pad, then assigns a wave (sample number) to
  // one of its slots. NOTE: message bytes match captures, but the full
  // sequence has not yet been driven against hardware.
  void SetPadWave(int kit, int pad, PadSlot slot, int sample,
      double pace_seconds = 0.02);

  using BlockCallback = std::function<void(const Bytes& block)>;

  // Streams a whole bank (0x10/0x20/0x30/0x40) and returns the reassembled
  // image: the block-frame payloads concatenated with their headers, the
  // same layout SplitBulkImage parses and the RE image cache stores. Reads
  // blocks until the device goes idle for idle_timeout. on_block, if given,
  // fires per block (for progress). NOTE: the request/streaming protocol is
  // reconstructed from captures but has NOT been driven live yet.
  Bytes DumpBank(uint8_t bank, const BlockCallback& on_block = {},
      double idle_timeout = 1.0, double block_timeout = 15.0);

 private:
  // Reads one transport frame's payload; empty on idle/malformed.
  Bytes ReadBulkFrame(double idle_timeout, double body_timeout);

  SerialPort port_;
};

}  // namespace spdsx::device

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_SPDSX_DEVICE_H_
