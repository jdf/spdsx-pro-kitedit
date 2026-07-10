// Sample playback: one sound per slot, start/stop/replace.
#pragma once

#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>

namespace spdsx {

class AudioEngine {
public:
  explicit AudioEngine(int slot_count);
  ~AudioEngine();
  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  // Loads (or replaces) the sound for a slot. False on decode failure.
  bool load(int slot, const juce::File& file);
  // Starts the slot's sound from the beginning; no-op for empty slots.
  void start(int slot);
  void stop(int slot);
  // True while the slot's sound is audibly playing (false once it ends).
  bool is_playing(int slot) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace spdsx
