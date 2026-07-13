// High-level connection to the SPD-SX PRO: the transport frame plus the
// device operations. Mirrors spdsx-py's SPDSX class.
//
// Every payload goes on the wire wrapped in the app's transport frame:
//   0d 60 e0 <ch> <8 junk bytes> 01 00 00 00 <len LE32> <payload>
// ch 0x07 = DT1 parameter writes, 0x08 = bulk data, 0x09 = control/status.
// Focus/select commands reply and must be drained; parameter writes don't
// ack.
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

// Scans /dev/cu.usbmodem* and pings each node until the device answers
// (the node number changes on every replug — there is no stable path).
// Returns the answering port; throws if none does (close the official
// app first: one program per port).
std::string FindDevicePort();

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
  // Reads one firmware-version ASCII field (control family cat 0x17,
  // decoded from a capture): field 0 = version ("2.00"), field 3 =
  // build ("0094"). Empty on no/short reply.
  std::string FirmwareField(uint8_t field);
  // Persists working changes to flash: control-family commit begin
  // (6a 03 21) then poll (6a 03 22) until the status word reads done.
  // Returns false on timeout. Same handshake the official app's WRITE
  // button uses.
  bool Commit(double timeout_seconds = 5.0);

  // Deletes a sample from the device pool by index, then commits. The
  // slot becomes empty; kits referencing it show no wave. Destructive
  // and not undoable on the device.
  void DeleteWave(int sample_index);

  // Uploads a wave to a pool index: writes its `.SMP` (RFWV) file to flash
  // and registers it in the pool directory so it appears in `samples` and
  // is assignable. Writing without registering leaves an orphan file no UI
  // can see, so upload is one atomic action. Live-verified 2026-07-13: the
  // uploaded sample shows in the pool, reads back byte-exact, and plays on
  // the device. (The record's 0x84 "content hash" field, whose algorithm
  // the official app uses but never shared, is written as 0 — the device
  // ignores it entirely; see WAVE-UPLOAD-DELETE-PROTOCOL.md.)
  void UploadWave(int sample_index, const Bytes& smp,
      const std::string& wavename, const std::string& filename);

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

  using ProgressCallback = std::function<void(size_t done, size_t total)>;

  // Reads a user wave's `.SMP` file off the device over the remote-file
  // protocol (family f0 41 7a, channel 0x06 — see
  // re-cache/captures/WAVE-EXPORT-PROTOCOL.md): OPEN the derived path,
  // STAT for the size, then loop READ (the device caps each reply at
  // ~512KB) until the whole file is in hand, then CLOSE. Returns the
  // raw `.SMP` bytes (RFWV header + PCM). Throws on protocol failure.
  // Preload waves aren't exportable; only user waves resolve.
  Bytes ReadRemoteWave(int sample_index,
      const ProgressCallback& on_progress = {}, double idle_timeout = 1.0);

  using BlockCallback = std::function<void(const Bytes& block)>;

  // Streams a whole bank (0x10/0x20/0x30/0x40) and returns the reassembled
  // image: the block-frame payloads concatenated with their headers, the
  // same layout SplitBulkImage parses and the RE image cache stores.
  // Follows the official app's handshake (live-verified 2026-07-12):
  // PREPARE all four banks, BEGIN the target, then repeated READs — each
  // yielding a device-chosen batch of 6c 02 blocks — until one yields
  // none, then END. on_block, if given, fires per block (for progress).
  Bytes DumpBank(uint8_t bank, const BlockCallback& on_block = {},
      double idle_timeout = 1.0, double block_timeout = 15.0);

 private:
  // Reads one transport frame's payload; empty on idle/malformed.
  Bytes ReadBulkFrame(double idle_timeout, double body_timeout);

  // Upload primitives, composed by UploadWave (never used separately — a
  // written-but-unregistered file is invisible to every UI).
  // WriteRemoteFile replicates the official app's channel-0x06 file write
  // (live-verified byte-exact); RegisterWave writes the two DT1 directory
  // records at 0x2000000 + N*256 and flash-commits.
  void WriteRemoteFile(int sample_index, const Bytes& smp);
  void RegisterWave(int sample_index, int frames,
      const std::string& wavename, const std::string& filename);

  SerialPort port_;
};

}  // namespace spdsx::device

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_SPDSX_DEVICE_H_
