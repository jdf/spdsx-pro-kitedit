// Rendering a local audio file into a device-playable `.SMP`.
//
// The SPD-SX PRO stores whatever it is given but only PLAYS 48 kHz /
// 16-bit material (mono or stereo), so the upload path decodes with
// JUCE's readers, resamples to 48 kHz, keeps the first two channels,
// and wraps the result in an RFWV image (device::PcmToRfwv supplies the
// header checksum the device validates).
#ifndef SPDSX_PATCHEDIT_SOURCE_SAMPLE_UPLOAD_H_
#define SPDSX_PATCHEDIT_SOURCE_SAMPLE_UPLOAD_H_

#include <juce_audio_formats/juce_audio_formats.h>

#include "device/sample_image.h"

namespace spdsx {

// The only sample rate the device plays.
inline constexpr uint32_t kDeviceSampleRate = 48000;

// Decodes `file` and builds the `.SMP` image described above. Returns an
// empty buffer and fills `error` when the file can't be read or is
// empty/absurd.
device::Bytes SmpFromAudioFile(const juce::File& file, juce::String& error);

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_SAMPLE_UPLOAD_H_
