// Sample playback: one sound per slot, play/pause/stop/replace.
#ifndef SPDSX_PATCHEDIT_SOURCE_AUDIO_H_
#define SPDSX_PATCHEDIT_SOURCE_AUDIO_H_

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
  std::optional<SampleInfo> Load(int slot, const juce::File& file);
  // Drops the slot's sound, if any.
  void Clear(int slot);
  // Starts or resumes from the current position; no-op for empty slots.
  void Play(int slot);
  // Stops, keeping the position (resume with play).
  void Pause(int slot);
  // Stops and rewinds to the top.
  void Stop(int slot);
  // Linear playback gain for the slot's next/current sound. Pad-level
  // triggers use this to realize layer-mode velocity scaling; slot-level
  // auditioning resets it to 1.
  void SetGain(int slot, float gain);
  // True while the slot's sound is audibly playing (false once it ends).
  bool IsPlaying(int slot) const;
  // Playback position as a fraction of the sample length, 0..1.
  double PositionFraction(int slot) const;

  // Auditions a file on a dedicated preview channel (browser autoplay),
  // separate from the slots. No-op on decode failure.
  void PreviewFile(const juce::File& file);
  void StopPreview();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_AUDIO_H_
