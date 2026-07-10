// Sample playback: one sound per slot, start/stop/replace.
#pragma once

#include <memory>
#include <string>

namespace spdsx {

class AudioEngine {
public:
  explicit AudioEngine(int slot_count);
  ~AudioEngine();
  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  // Loads (or replaces) the sound for a slot. False on decode failure.
  bool load(int slot, const std::string& wav_path);
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
