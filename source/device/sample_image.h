// Parsing the bank 0x20 (sample directory) bulk-read image into the
// device's wave pool listing.
//
// Bank 0x20 holds NO audio — it is the sample *directory*: 20001
// fixed-size slots (the device's 20,000-wave capacity plus the index-0
// "no sample" sentinel), followed by the 22-entry category name table
// and the user-tag name table. The wave audio itself lives in the
// device's internal storage, addressed by an opaque 64-bit pointer in
// each record, and does not appear anywhere in the state dump; pulling
// audio needs a transfer protocol that is still uncaptured.
//
// Record layout (reverse-engineered 2026-07-12 from the cached image;
// index verified: record 127 == "Kick ProcElec 28", the sample number
// pad wave assignment uses):
//   - 164 (0xa4) bytes per record, record N == sample index N
//   - +0x00 wavename[16]   space-padded ASCII (the display name)
//   - +0x10 filename[84]   original file name, space-padded
//   - +0x8c u64 LE         internal-storage pointer (opaque)
//   - +0x94 u32 LE         length (sample frames; ~48kHz material)
//   - +0x9c u32 LE         unknown (preloads 273..383, user waves 127)
//   - +0xa0 u32 LE         category, 0..21 (kSampleCategoryNames)
// The directory is located by anchoring on the "PRELOAD 00001" record,
// so any clean image containing bank 0x20 works.
#ifndef SPDSX_PATCHEDIT_SOURCE_DEVICE_SAMPLE_IMAGE_H_
#define SPDSX_PATCHEDIT_SOURCE_DEVICE_SAMPLE_IMAGE_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "device/protocol.h"  // Bytes

namespace spdsx::device {

// Directory slots including the index-0 sentinel.
inline constexpr int kSampleSlots = 20001;
inline constexpr size_t kSampleRecordStride = 164;  // 0xa4

// The device's wave categories, indexed by the record's category field.
inline constexpr int kSampleCategoryCount = 22;
std::string_view SampleCategoryName(int category);

// One named wave in the device pool.
struct SampleRecord {
  int index = 0;         // pool index; what pad wave assignment references
  std::string wavename;  // display name
  std::string filename;  // original file name
  uint32_t frames = 0;   // length in sample frames
  int category = 0;      // 0..21

  // Factory preload waves can never be exported off the device, so
  // they will never have local audio; user imports eventually will.
  bool is_preload() const { return filename.rfind("PRELOAD ", 0) == 0; }
};

// Parses the sample directory out of a CLEAN (header-stripped) image
// containing bank 0x20. Returns the named records only, in index
// order; empty if the directory can't be located.
std::vector<SampleRecord> ParseSampleDir(const Bytes& clean_image);

// The device-side path a user wave is read from over the remote-file
// protocol (family f0 41 7a, channel 0x06 — see
// re-cache/captures/WAVE-EXPORT-PROTOCOL.md). Sample index N lives at
// /SPDSXREMOTE//Roland/SPD-SXPRO/WAVE/DATA/D<N/100>/W<N>.SMP.
std::string RemoteWavePath(int index);

// The header of a `.SMP` (RFWV) wave file (32 bytes, verified against a
// device export): magic, data length (= file size - 8), sample rate,
// channels, and bits/sample; signed LE PCM follows at kRfwvHeaderSize.
inline constexpr size_t kRfwvHeaderSize = 32;
struct RfwvHeader {
  bool valid = false;
  uint32_t data_bytes = 0;
  uint32_t sample_rate = 0;
  uint16_t channels = 0;
  uint16_t bits_per_sample = 0;
};
// Parses an RFWV header from the start of a `.SMP` payload.
RfwvHeader ParseRfwvHeader(const Bytes& smp);

}  // namespace spdsx::device

#endif  // SPDSX_PATCHEDIT_SOURCE_DEVICE_SAMPLE_IMAGE_H_
