// Sample playback: one sound per slot, play/pause/stop/replace.
#pragma once

#include <memory>
#include <optional>

#include <juce_audio_utils/juce_audio_utils.h>

namespace spdsx {

struct SampleInfo {
  double duration_seconds = 0.0;
  double sample_rate = 0.0;
};

class AudioEngine {
public:
  explicit AudioEngine(int slot_count);
  ~AudioEngine();
  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  // Loads (or replaces) the sound for a slot. Empty on decode failure.
  std::optional<SampleInfo> load(int slot, const juce::File& file);
  // Drops the slot's sound, if any.
  void clear(int slot);
  // Starts or resumes from the current position; no-op for empty slots.
  void play(int slot);
  // Stops, keeping the position (resume with play).
  void pause(int slot);
  // Stops and rewinds to the top.
  void stop(int slot);
  // True while the slot's sound is audibly playing (false once it ends).
  bool is_playing(int slot) const;
  // Playback position as a fraction of the sample length, 0..1.
  double position_fraction(int slot) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace spdsx
