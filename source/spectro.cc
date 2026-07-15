#include "spectro.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

#include <specgram/dsp.h>
#include <specgram/render.h>

namespace spdsx {

std::string render_spectrogram(const std::string& wav_path, int slot) {
  std::vector<float> mono;
  specgram::AudioMeta meta;
  if (!specgram::read_audio_mono(wav_path.c_str(), mono, meta)) {
    return "";
  }
  size_t num_frames = 0;
  size_t num_bins = 0;
  std::vector<float> db = specgram::stft_db(mono, num_frames, num_bins);
  if (num_frames == 0) {
    return "";
  }

  // Bare plot, no chrome: the slot rectangle is the frame.
  specgram::RenderOptions opts;
  opts.layout.width = 560;
  opts.layout.height = 160;
  opts.layout.margin_left = 0;
  opts.layout.margin_top = 0;
  opts.layout.margin_right = 0;
  opts.layout.margin_bottom = 0;

  auto dir = std::filesystem::temp_directory_path() / "spdsx-patchedit";
  std::filesystem::create_directories(dir);
  // Generation counter in the name so replacing a slot's sample never
  // collides with Slint's by-path image cache.
  static std::atomic<int> generation {0};
  auto out = dir
      / ("slot-" + std::to_string(slot) + "-" + std::to_string(generation++)
         + ".png");

  if (!specgram::render_png(
          db, num_frames, num_bins, meta, opts, out.string().c_str())) {
    return "";
  }
  return out.string();
}

}  // namespace spdsx
