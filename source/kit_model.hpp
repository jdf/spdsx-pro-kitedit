// The kit: a name and nine pads, each carrying a top and bottom sample.
// This is the source of truth the UI and audio engine react to, the unit
// that undo mutates, and the thing that gets serialized to .kit files.
#pragma once

#include <array>
#include <utility>

#include <juce_core/juce_core.h>

namespace spdsx {

struct Pad {
  // top, bottom; an empty File means the layer holds no sample.
  std::pair<juce::File, juce::File> samples;
  // There will be other per-pad properties coming soon.
};

class KitModel {
public:
  static constexpr int kPadCount = 9;
  static constexpr int kLayersPerPad = 2;
  // The UI still sizes its flat slot arrays from this.
  static constexpr int kSlotCount = kPadCount * kLayersPerPad;

  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void KitNameChanged() {}
    virtual void SampleChanged(int pad, int layer)
    {
      (void)pad;
      (void)layer;
    }
  };

  const juce::String& name() const { return name_; }
  void set_name(const juce::String& name)
  {
    if (name_ != name) {
      name_ = name;
      listeners_.call([](Listener& l) { l.KitNameChanged(); });
    }
  }

  const Pad& pad(int idx) const
  {
    return pads_.at(static_cast<size_t>(idx));
  }

  // layer 0 is the top sample, 1 the bottom.
  const juce::File& sample(int pad, int layer) const
  {
    const auto& samples = pads_.at(static_cast<size_t>(pad)).samples;
    return layer == 0 ? samples.first : samples.second;
  }
  void set_sample(int pad, int layer, const juce::File& file)
  {
    auto& samples = pads_.at(static_cast<size_t>(pad)).samples;
    auto& current = layer == 0 ? samples.first : samples.second;
    if (current != file) {
      current = file;
      listeners_.call(
          [pad, layer](Listener& l) { l.SampleChanged(pad, layer); });
    }
  }

  void AddListener(Listener* listener) { listeners_.add(listener); }
  void RemoveListener(Listener* listener)
  {
    listeners_.remove(listener);
  }

private:
  juce::String name_ {"Untitled Kit"};
  std::array<Pad, kPadCount> pads_;
  juce::ListenerList<Listener> listeners_;
};

}  // namespace spdsx
