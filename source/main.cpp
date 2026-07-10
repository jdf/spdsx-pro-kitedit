#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "app-window.h"
#include "audio.hpp"
#include "spectro.hpp"

namespace {

constexpr int kSlotCount = 18;

void load_sample(spdsx::AudioEngine& engine,
    slint::VectorModel<SlotData>& slots,
    int idx,
    const std::string& path)
{
  if (idx < 0 || idx >= kSlotCount) {
    std::fprintf(stderr, "slot %d out of range (0..%d)\n", idx, kSlotCount - 1);
    return;
  }
  if (!engine.load(idx, path)) {
    return;
  }
  auto row = *slots.row_data(static_cast<size_t>(idx));
  row.path = slint::SharedString(path);
  row.name =
      slint::SharedString(std::filesystem::path(path).filename().string());
  row.has_sample = true;
  row.playing = false;
  // Too-short files play fine but render no spectrogram; leave the image
  // empty in that case.
  if (auto png = spdsx::render_spectrogram(path, idx); !png.empty()) {
    row.spectrogram = slint::Image::load_from_path(slint::SharedString(png));
  } else {
    row.spectrogram = slint::Image {};
  }
  slots.set_row_data(static_cast<size_t>(idx), row);
}

}  // namespace

int main(int argc, char** argv)
{
  auto ui = AppWindow::create();

  auto slots = std::make_shared<slint::VectorModel<SlotData>>();
  for (int i = 0; i < kSlotCount; ++i) {
    slots->push_back(SlotData {});
  }
  ui->set_slots(slots);

  spdsx::AudioEngine engine(kSlotCount);

  // --load <slot> <wav> pre-fills a slot; useful for testing without
  // drag and drop. May be repeated.
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--load" && i + 2 < argc) {
      load_sample(engine, *slots, std::atoi(argv[i + 1]), argv[i + 2]);
      i += 2;
    } else {
      std::fprintf(stderr, "usage: %s [--load <slot> <file.wav>]...\n", argv[0]);
      return 2;
    }
  }

  ui->on_toggle_play(
      [&](int idx)
      {
        if (idx < 0 || idx >= kSlotCount) {
          return;
        }
        auto row = *slots->row_data(static_cast<size_t>(idx));
        if (!row.has_sample) {
          return;
        }
        if (engine.is_playing(idx)) {
          engine.stop(idx);
          row.playing = false;
        } else {
          engine.start(idx);
          row.playing = true;
        }
        slots->set_row_data(static_cast<size_t>(idx), row);
      });

  // Sounds end on the audio thread; poll so the playing highlight clears
  // when a sample runs out on its own.
  slint::Timer poll;
  poll.start(slint::TimerMode::Repeated, std::chrono::milliseconds(100),
      [&]
      {
        for (int i = 0; i < kSlotCount; ++i) {
          auto row = *slots->row_data(static_cast<size_t>(i));
          if (row.playing && !engine.is_playing(i)) {
            row.playing = false;
            slots->set_row_data(static_cast<size_t>(i), row);
          }
        }
      });

  ui->run();
  return 0;
}
