// The kit: a name and 18 sample assignments. This is the source of
// truth the UI and audio engine react to, the unit that undo mutates,
// and the thing that gets serialized to .kit files.
#pragma once

#include <array>

#include <juce_core/juce_core.h>

namespace spdsx {

class KitModel {
public:
  static constexpr int kSlotCount = 18;

  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void kit_name_changed() {}
    virtual void slot_changed(int idx) { (void)idx; }
  };

  const juce::String& name() const { return name_; }
  void set_name(const juce::String& name)
  {
    if (name_ != name) {
      name_ = name;
      listeners_.call([](Listener& l) { l.kit_name_changed(); });
    }
  }

  // An empty File means the slot holds no sample.
  const juce::File& slot(int idx) const
  {
    return slots_.at(static_cast<size_t>(idx));
  }
  void set_slot(int idx, const juce::File& file)
  {
    auto& current = slots_.at(static_cast<size_t>(idx));
    if (current != file) {
      current = file;
      listeners_.call([idx](Listener& l) { l.slot_changed(idx); });
    }
  }

  void add_listener(Listener* listener) { listeners_.add(listener); }
  void remove_listener(Listener* listener)
  {
    listeners_.remove(listener);
  }

private:
  juce::String name_ {"Untitled Kit"};
  std::array<juce::File, kSlotCount> slots_;
  juce::ListenerList<Listener> listeners_;
};

}  // namespace spdsx
