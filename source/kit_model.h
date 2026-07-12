// The kit: a name and nine pads, each carrying a top and bottom sample.
// This is the source of truth the UI and audio engine react to, the unit
// that undo mutates, and the thing that gets serialized to .kit files.
#ifndef SPDSX_PATCHEDIT_SOURCE_KIT_MODEL_H_
#define SPDSX_PATCHEDIT_SOURCE_KIT_MODEL_H_

#include <array>
#include <utility>

#include <juce_core/juce_core.h>

#include "layers.h"

namespace spdsx {

struct Pad {
  // top, bottom; an empty File means the layer holds no sample.
  std::pair<juce::File, juce::File> samples;
  // How the two layers respond to a hit, and the velocity thresholds the
  // fade/switch modes read (1..127).
  LayerMode mode = LayerMode::kMix;
  int fade_point = kDefaultFadePoint;
  int fade_end = kDefaultFadeEnd;
};

class KitModel {
public:
  static constexpr int kPadCount = 9;
  static constexpr int kLayersPerPad = 2;
  // The UI still sizes its flat slot arrays from this.
  static constexpr int kSlotCount = kPadCount * kLayersPerPad;

  // Pads start in their default modes (pad 9 is the hi-hat).
  KitModel()
  {
    for (int i = 0; i < kPadCount; ++i) {
      pads_[static_cast<size_t>(i)].mode = DefaultLayerMode(i);
    }
  }

  // Pad 9 (bottom-right) defaults to HI-HAT, the rest to MIX.
  static LayerMode DefaultLayerMode(int pad)
  {
    return pad == kPadCount - 1 ? LayerMode::kHiHat : LayerMode::kMix;
  }

  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void KitNameChanged() {}
    virtual void SampleChanged(int pad, int layer)
    {
      (void)pad;
      (void)layer;
    }
    // Layer mode or fade parameters changed.
    virtual void PadParamsChanged(int pad) { (void)pad; }
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

  LayerMode layer_mode(int pad) const
  {
    return pads_.at(static_cast<size_t>(pad)).mode;
  }
  int fade_point(int pad) const
  {
    return pads_.at(static_cast<size_t>(pad)).fade_point;
  }
  int fade_end(int pad) const
  {
    return pads_.at(static_cast<size_t>(pad)).fade_end;
  }
  // One notification even when several of the three change together.
  void SetLayerParams(int pad, LayerMode mode, int fade_point, int fade_end)
  {
    auto& p = pads_.at(static_cast<size_t>(pad));
    if (p.mode != mode || p.fade_point != fade_point
        || p.fade_end != fade_end)
    {
      p.mode = mode;
      p.fade_point = fade_point;
      p.fade_end = fade_end;
      listeners_.call([pad](Listener& l) { l.PadParamsChanged(pad); });
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

#endif  // SPDSX_PATCHEDIT_SOURCE_KIT_MODEL_H_
