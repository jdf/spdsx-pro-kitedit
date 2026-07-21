#include "sample_upload.h"

#include <cmath>
#include <memory>

#include <gtest/gtest.h>

#include "temp_dir.h"

namespace spdsx {
namespace {

// Writes a sine WAV at the given rate/channels so conversions have a
// real, decodable input with known length and content.
juce::File WriteWav(const juce::File& file,
                    double sample_rate,
                    int channels,
                    int frames) {
  juce::WavAudioFormat format;
  auto stream = file.createOutputStream();
  std::unique_ptr<juce::AudioFormatWriter> writer(
      format.createWriterFor(stream.get(),
                             sample_rate,
                             static_cast<unsigned int>(channels),
                             16,
                             {},
                             0));
  if (writer == nullptr) {
    return {};
  }
  stream.release();  // the writer owns it now
  juce::AudioBuffer<float> buffer(channels, frames);
  for (int ch = 0; ch < channels; ++ch) {
    for (int i = 0; i < frames; ++i) {
      buffer.setSample(
          ch,
          i,
          0.5f
              * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i
                         / static_cast<float>(sample_rate)));
    }
  }
  writer->writeFromAudioSampleBuffer(buffer, 0, frames);
  return file;
}

class SampleUploadTest : public ::testing::Test {
protected:
  spdsx_testing::TempDir temp;
  juce::String error;
};

TEST_F(SampleUploadTest, A48kFilePassesThroughAtFullLength) {
  const juce::File wav = WriteWav(temp.file("a.wav"), 48000, 1, 4800);
  ASSERT_TRUE(wav.existsAsFile());

  const device::Bytes smp = SmpFromAudioFile(wav, error);

  ASSERT_FALSE(smp.empty()) << error;
  const device::RfwvHeader header = device::ParseRfwvHeader(smp);
  ASSERT_TRUE(header.valid);
  EXPECT_EQ(header.sample_rate, 48000u);
  EXPECT_EQ(header.bits_per_sample, 16u);
  EXPECT_EQ(header.channels, 1u);
  EXPECT_EQ(smp.size() - device::kRfwvHeaderSize, 4800u * 2u);
}

TEST_F(SampleUploadTest, A44k1FileResamplesTo48k) {
  const juce::File wav = WriteWav(temp.file("b.wav"), 44100, 1, 44100);
  ASSERT_TRUE(wav.existsAsFile());

  const device::Bytes smp = SmpFromAudioFile(wav, error);

  ASSERT_FALSE(smp.empty()) << error;
  const device::RfwvHeader header = device::ParseRfwvHeader(smp);
  ASSERT_TRUE(header.valid);
  EXPECT_EQ(header.sample_rate, 48000u);
  // One second of input stays one second of output: 48000 frames,
  // within a frame of rounding.
  const auto frames = (smp.size() - device::kRfwvHeaderSize) / 2;
  EXPECT_NEAR(static_cast<double>(frames), 48000.0, 2.0);
}

TEST_F(SampleUploadTest, StereoStaysStereo) {
  const juce::File wav = WriteWav(temp.file("c.wav"), 48000, 2, 1000);
  ASSERT_TRUE(wav.existsAsFile());

  const device::Bytes smp = SmpFromAudioFile(wav, error);

  ASSERT_FALSE(smp.empty()) << error;
  EXPECT_EQ(device::ParseRfwvHeader(smp).channels, 2u);
  EXPECT_EQ(smp.size() - device::kRfwvHeaderSize, 1000u * 2u * 2u);
}

// The device validates the header checksum before playing; PcmToRfwv is
// trusted for its math, but the conversion must actually go through it.
TEST_F(SampleUploadTest, TheResultCarriesANonZeroChecksum) {
  const juce::File wav = WriteWav(temp.file("d.wav"), 48000, 1, 100);
  const device::Bytes smp = SmpFromAudioFile(wav, error);

  ASSERT_FALSE(smp.empty()) << error;
  bool any = false;
  for (size_t i = 0x20; i < 0x30; ++i) {
    any |= smp[i] != 0;
  }
  EXPECT_TRUE(any);
}

TEST_F(SampleUploadTest, RefusesAFileThatIsNotAudio) {
  const juce::File not_audio = temp.file("kit.json");
  not_audio.replaceWithText("{}");

  EXPECT_TRUE(SmpFromAudioFile(not_audio, error).empty());
  EXPECT_TRUE(error.isNotEmpty());
}

TEST_F(SampleUploadTest, RefusesAMissingFile) {
  EXPECT_TRUE(SmpFromAudioFile(temp.file("absent.wav"), error).empty());
  EXPECT_TRUE(error.isNotEmpty());
}

}  // namespace
}  // namespace spdsx
