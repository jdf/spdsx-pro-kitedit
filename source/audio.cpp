#include "audio.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <cstdio>
#include <vector>

namespace spdsx {

namespace {

// ma_sound is neither movable nor copyable; hold each behind a pointer and
// tear it down before the engine.
struct Sound {
  explicit Sound(ma_engine& engine, const char* path)
  {
    ok = ma_sound_init_from_file(
             &engine, path, MA_SOUND_FLAG_DECODE, nullptr, nullptr, &sound)
        == MA_SUCCESS;
  }
  ~Sound()
  {
    if (ok) {
      ma_sound_uninit(&sound);
    }
  }
  ma_sound sound {};
  bool ok = false;
};

}  // namespace

struct AudioEngine::Impl {
  ma_engine engine {};
  bool engine_ok = false;
  std::vector<std::unique_ptr<Sound>> sounds;
};

AudioEngine::AudioEngine(int slot_count)
    : impl_(std::make_unique<Impl>())
{
  impl_->engine_ok = ma_engine_init(nullptr, &impl_->engine) == MA_SUCCESS;
  if (!impl_->engine_ok) {
    std::fprintf(stderr, "audio: failed to initialize output device\n");
  }
  impl_->sounds.resize(static_cast<size_t>(slot_count));
}

AudioEngine::~AudioEngine()
{
  impl_->sounds.clear();
  if (impl_->engine_ok) {
    ma_engine_uninit(&impl_->engine);
  }
}

bool AudioEngine::load(int slot, const std::string& wav_path)
{
  if (!impl_->engine_ok) {
    return false;
  }
  auto sound = std::make_unique<Sound>(impl_->engine, wav_path.c_str());
  if (!sound->ok) {
    std::fprintf(stderr, "audio: cannot load '%s'\n", wav_path.c_str());
    return false;
  }
  impl_->sounds.at(static_cast<size_t>(slot)) = std::move(sound);
  return true;
}

void AudioEngine::start(int slot)
{
  auto& sound = impl_->sounds.at(static_cast<size_t>(slot));
  if (!sound) {
    return;
  }
  ma_sound_seek_to_pcm_frame(&sound->sound, 0);
  ma_sound_start(&sound->sound);
}

void AudioEngine::stop(int slot)
{
  auto& sound = impl_->sounds.at(static_cast<size_t>(slot));
  if (sound) {
    ma_sound_stop(&sound->sound);
  }
}

bool AudioEngine::is_playing(int slot) const
{
  const auto& sound = impl_->sounds.at(static_cast<size_t>(slot));
  return sound && ma_sound_is_playing(&sound->sound);
}

}  // namespace spdsx
