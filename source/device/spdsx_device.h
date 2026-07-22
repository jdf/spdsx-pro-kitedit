// High-level connection to the SPD-SX PRO: the transport frame plus the
// device operations. Mirrors spdsx-py's SPDSX class.
//
// Every payload goes on the wire wrapped in the app's transport frame:
//   0d 60 e0 <ch> <8 junk bytes> 01 00 00 00 <len LE32> <payload>
// ch 0x07 = DT1 parameter writes, 0x08 = bulk data, 0x09 = control/status.
// DT1 writes — parameters, kit/object selects, directory records — are
// NEVER acked (no capture shows a device frame on the DT1 channel except
// unsolicited notifications); control, bulk and file commands all reply.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_SPDSX_DEVICE_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_SPDSX_DEVICE_H_

#include <functional>
#include <string>

#include "absl/functional/function_ref.h"
#include "device/kit_image.h"  // PadDeviceParams
#include "device/protocol.h"
#include "device/serial_port.h"

namespace spdsx::device {

// The default "keep waiting" predicate for Commit — never asks to stop.
// A named function (static lifetime) is a safe default for a FunctionRef.
inline bool NeverAbort() {
  return false;
}

// Wraps a payload in the transport frame; the channel is chosen from the
// payload's message family.
Bytes Wrap(const Bytes& payload);
// Extracts the payload from a framed reply; empty if malformed.
Bytes Unwrap(const Bytes& frame);

// Pings each of the backend's candidate nodes until the device answers, and
// returns that one; throws if none does (close the official app first: one
// program per port). Nodes that won't open are skipped — another program may
// hold one, or it may have gone since it was listed.
//
// The backend is where the platform lives; the trying is not, so this takes
// one rather than assuming. Defaults to the real one.
std::string FindDevicePort(PortBackend& ports = PlatformPorts());

class SpdsxDevice {
public:
  // Borrows the port: it must outlive the device. Taking the channel rather
  // than a path is what lets a test drive every op against a fake.
  explicit SpdsxDevice(SerialPort* port);

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
  // Polls with NO time limit — a batch commit can take many seconds, and
  // a timeout that fired mid-commit misreported it and risked interrupting
  // the flash. Pass should_abort to stop waiting (a user Abort); returning
  // false then makes no claim the commit finished. Same handshake the
  // official app's WRITE button uses.
  bool Commit(absl::FunctionRef<bool()> should_abort = NeverAbort);

  // The commit that ends an upload batch. Same begin/poll as Commit, plus
  // the two control messages (6a 0c arg 1, 6a 02) the official app sends
  // between them after every import (synthupload-1.log, import-multi-1.log)
  // — and only after imports; a plain WRITE or a delete commits without.
  bool CommitUploadBatch(absl::FunctionRef<bool()> should_abort = NeverAbort);

  // Deletes a sample from the device pool by index, then commits — the
  // official app's full sequence: slot + last-index queries, the delete in
  // a session bracket, the post-delete status queries, then the commit. The
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
  void UploadWave(int sample_index,
                  const Bytes& smp,
                  const std::string& wavename,
                  const std::string& filename);

  // Opens an upload batch (call once before a run of UploadWave calls): the
  // official app's one 6a 0d last-registered-index query (import-multi-1.log
  // preamble; we pick indices from the bulk image, so the answer is
  // drained and dropped). UploadWave leaves everything in working state; a
  // single CommitUploadBatch() after the whole batch flushes it to flash.
  // Committing per file wedges the device after a few uploads — the
  // official app commits the batch once.
  void PrepareUploadBatch();

  // Kit/object selects are DT1 writes like any other: fire-and-forget, no
  // ack to wait for (waiting for one was a hidden 0.4s stall per call).
  // CAUTION: if the port will close right after, follow the send with any
  // round trip (Ping is the cheap one) — closing a CDC port can kill an
  // in-flight write, and tcdrain does not save it (live, 2026-07-22).
  void SelectKit(int kit);
  void SelectObject(ObjectKind kind, int index);
  // Focus the object, then write the pad-link group for a specific kit;
  // pace_seconds paces the no-ack writes.
  void SetPadLink(int kit,
                  ObjectKind kind,
                  int index,
                  int group,
                  double pace_seconds = 0.02);

  // Selects the kit, then writes its 16-char name (space-padded/truncated).
  void SetKitName(int kit, const std::string& name, double pace_seconds = 0.02);

  // Selects the kit, focuses the pad, then assigns a wave (sample number) to
  // one of its slots. NOTE: message bytes match captures, but the full
  // sequence has not yet been driven against hardware.
  void SetPadWave(
      int kit, int pad, PadSlot slot, int sample, double pace_seconds = 0.02);

  // Selects the kit, focuses the pad, then writes its hit-response layer
  // params (mode, fades, dynamics, curve, fixed velocity, hi-hat
  // closed-pedal volume/fade-in/decay, trigger reserve) as one DT1 per
  // field. pad is 1-based. Changes hit working state; persist with
  // Commit(). NOTE: message bytes match captures, not yet driven end-to-
  // end against hardware.
  void SetPadLayerParams(int kit,
                         int pad,
                         const PadDeviceParams& params,
                         double pace_seconds = 0.02);

  using ProgressCallback = std::function<void(size_t done, size_t total)>;

  // Reads a user wave's `.SMP` file off the device over the remote-file
  // protocol (family f0 41 7a, channel 0x06), in the official app's exact
  // order (waveexport-1/2.log): session open (6a 09/0a), OPEN the derived
  // path, read the 512-byte RFWV header, STAT for the size, then read the
  // remainder in exact-size requests of at most 512 KiB, CLOSE, session
  // close (6a 09 arg 0). Returns the raw `.SMP` bytes (header + PCM).
  // Throws on protocol failure. Preload waves aren't exportable; only user
  // waves resolve.
  Bytes ReadRemoteWave(int sample_index,
                       const ProgressCallback& on_progress = {},
                       double idle_timeout = 1.0);

  using BlockCallback = std::function<void(const Bytes& block)>;

  // Streams a whole bank (0x10/0x20/0x30/0x40) and returns the reassembled
  // image: the block-frame payloads concatenated with their headers, the
  // same layout SplitBulkImage parses and the RE image cache stores.
  // Follows the official app's handshake (live-verified 2026-07-12):
  // PREPARE all four banks, BEGIN the target, then repeated READs — each
  // yielding a device-chosen batch of 6c 02 blocks — until one yields
  // none, then END. on_block, if given, fires per block (for progress).
  Bytes DumpBank(uint8_t bank,
                 const BlockCallback& on_block = {},
                 double idle_timeout = 1.0,
                 double block_timeout = 15.0);

private:
  // Reads one transport frame's payload; empty on idle/malformed.
  Bytes ReadBulkFrame(double idle_timeout, double body_timeout);

  // Upload primitives, composed by UploadWave (never used separately — a
  // written-but-unregistered file is invisible to every UI).
  // WriteRemoteFile replicates the official app's channel-0x06 file write
  // (live-verified byte-exact); RegisterWave writes the two DT1 directory
  // records at 0x2000000 + N*256 around the session close. Neither
  // flash-commits: the batch commits once, in CommitUploadBatch.
  void WriteRemoteFile(int sample_index, const Bytes& smp);
  void RegisterWave(int sample_index,
                    int frames,
                    const std::string& wavename,
                    const std::string& filename);

  // The shared tail of Commit/CommitUploadBatch: poll 6a 03 22 until the
  // status word reads done, with no time limit.
  bool PollCommitted(absl::FunctionRef<bool()> should_abort);

  SerialPort* port_;  // borrowed; outlives this
};

}  // namespace spdsx::device

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_SPDSX_DEVICE_H_
