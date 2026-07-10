#include <cstdio>
#include <memory>

#include "app-window.h"

int main()
{
  auto ui = AppWindow::create();

  auto slots = std::make_shared<slint::VectorModel<SlotData>>();
  for (int i = 0; i < 18; ++i) {
    slots->push_back(SlotData {});
  }
  ui->set_slots(slots);

  // Stub until the audio engine lands: prove the spacebar reaches the slot
  // under the mouse.
  ui->on_toggle_play([](int idx) { std::printf("toggle slot %d\n", idx); });

  ui->run();
  return 0;
}
