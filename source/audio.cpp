#include "audio.hpp"

#include <cstdio>
#include <vector>

namespace spdsx {

namespace {

struct Slot {
  std::unique_ptr<juce::AudioFormatReaderSource> reader_source;
  juce::AudioTransportSource transport;
};

}  // namespace

struct AudioEngine::Impl {
  juce::AudioDeviceManager device_manager;
  juce::AudioSourcePlayer player;
  juce::MixerAudioSource mixer;
  juce::AudioFormatManager formats;
  std::vector<std::unique_ptr<Slot>> slots;
};

AudioEngine::AudioEngine(int slot_count)
    : impl_(std::make_unique<Impl>())
{
  impl_->formats.registerBasicFormats();
  auto err = impl_->device_manager.initialiseWithDefaultDevices(0, 2);
  if (err.isNotEmpty()) {
    std::fprintf(stderr, "audio: %s\n", err.toRawUTF8());
  }
  for (int i = 0; i < slot_count; ++i) {
    auto slot = std::make_unique<Slot>();
    // A transport with no source renders silence, so idle slots cost
    // nothing but a mix input.
    impl_->mixer.addInputSource(&slot->transport, false);
    impl_->slots.push_back(std::move(slot));
  }
  impl_->player.setSource(&impl_->mixer);
  impl_->device_manager.addAudioCallback(&impl_->player);
}

AudioEngine::~AudioEngine()
{
  impl_->device_manager.removeAudioCallback(&impl_->player);
  impl_->player.setSource(nullptr);
  impl_->mixer.removeAllInputs();
  for (auto& slot : impl_->slots) {
    slot->transport.setSource(nullptr);
  }
}

std::optional<SampleInfo> AudioEngine::Load(int slot, const juce::File& file)
{
  auto& s = *impl_->slots.at(static_cast<size_t>(slot));
  auto* reader = impl_->formats.createReaderFor(file);
  if (reader == nullptr) {
    std::fprintf(stderr, "audio: cannot load '%s'\n",
        file.getFullPathName().toRawUTF8());
    return std::nullopt;
  }
  SampleInfo info;
  info.sample_rate = reader->sampleRate;
  if (reader->sampleRate > 0) {
    info.duration_seconds =
        static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
  }
  s.transport.stop();
  s.transport.setSource(nullptr);
  s.reader_source =
      std::make_unique<juce::AudioFormatReaderSource>(reader, true);
  s.transport.setSource(
      s.reader_source.get(), 0, nullptr, reader->sampleRate);
  return info;
}

void AudioEngine::Clear(int slot)
{
  auto& s = *impl_->slots.at(static_cast<size_t>(slot));
  s.transport.stop();
  s.transport.setSource(nullptr);
  s.reader_source.reset();
}

void AudioEngine::Play(int slot)
{
  auto& s = *impl_->slots.at(static_cast<size_t>(slot));
  if (s.reader_source != nullptr) {
    s.transport.start();
  }
}

void AudioEngine::Pause(int slot)
{
  impl_->slots.at(static_cast<size_t>(slot))->transport.stop();
}

void AudioEngine::Stop(int slot)
{
  auto& s = *impl_->slots.at(static_cast<size_t>(slot));
  s.transport.stop();
  s.transport.setPosition(0.0);
}

bool AudioEngine::IsPlaying(int slot) const
{
  return impl_->slots.at(static_cast<size_t>(slot))->transport.isPlaying();
}

double AudioEngine::PositionFraction(int slot) const
{
  auto& s = *impl_->slots.at(static_cast<size_t>(slot));
  const double length = s.transport.getLengthInSeconds();
  if (length <= 0.0) {
    return 0.0;
  }
  return juce::jlimit(0.0, 1.0, s.transport.getCurrentPosition() / length);
}

}  // namespace spdsx
