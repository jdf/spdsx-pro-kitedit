#include "sample_upload.h"

#include <algorithm>
#include <cmath>

namespace spdsx {

namespace {

// Ten minutes of stereo at 48 kHz is ~330 MB of PCM — far beyond any
// pad sample, and a fine place to refuse rather than exhaust memory on
// a mistaken drop of, say, a DJ set.
constexpr juce::int64 kMaxInputFrames = 1 << 28;

}  // namespace

device::Bytes SmpFromAudioFile(const juce::File& file, juce::String& error) {
  juce::AudioFormatManager manager;
  manager.registerBasicFormats();
  const std::unique_ptr<juce::AudioFormatReader> reader(
      manager.createReaderFor(file));
  if (reader == nullptr) {
    error = file.getFileName() + " is not an audio file this app can read";
    return {};
  }
  if (reader->lengthInSamples <= 0) {
    error = file.getFileName() + " holds no audio";
    return {};
  }
  if (reader->lengthInSamples > kMaxInputFrames) {
    error = file.getFileName() + " is too long to upload as a pad sample";
    return {};
  }

  const int in_frames = static_cast<int>(reader->lengthInSamples);
  const int channels = std::min(2, static_cast<int>(reader->numChannels));
  juce::AudioBuffer<float> in(static_cast<int>(reader->numChannels), in_frames);
  if (!reader->read(&in, 0, in_frames, 0, true, true)) {
    error = "couldn't decode " + file.getFileName();
    return {};
  }

  // Resample to the one rate the device plays. The Lagrange interpolator
  // is JUCE's standard offline choice; each channel converts alone.
  juce::AudioBuffer<float> out;
  if (static_cast<uint32_t>(reader->sampleRate) == kDeviceSampleRate) {
    out.makeCopyOf(in);
  } else {
    const double ratio = reader->sampleRate / kDeviceSampleRate;
    const int out_frames =
        std::max(1, static_cast<int>(std::floor(in_frames / ratio)));
    out.setSize(channels, out_frames);
    for (int ch = 0; ch < channels; ++ch) {
      juce::LagrangeInterpolator interpolator;
      interpolator.process(
          ratio, in.getReadPointer(ch), out.getWritePointer(ch), out_frames);
    }
  }

  // Interleave to little-endian 16-bit PCM, the device's playback format.
  const int frames = out.getNumSamples();
  device::Bytes pcm;
  pcm.reserve(static_cast<size_t>(frames) * static_cast<size_t>(channels) * 2);
  for (int i = 0; i < frames; ++i) {
    for (int ch = 0; ch < channels; ++ch) {
      const float v = juce::jlimit(-1.0f, 1.0f, out.getSample(ch, i));
      const auto s = static_cast<int16_t>(std::lround(v * 32767.0f));
      pcm.push_back(static_cast<uint8_t>(s & 0xFF));
      pcm.push_back(static_cast<uint8_t>((s >> 8) & 0xFF));
    }
  }
  return device::PcmToRfwv(
      pcm, kDeviceSampleRate, static_cast<uint16_t>(channels), 16);
}

}  // namespace spdsx
