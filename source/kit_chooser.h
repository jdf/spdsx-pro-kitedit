// The header's unified kit control: prev/next arrows around a chip
// showing the kit number and name. Clicking the chip drops down the
// full kit menu; the pencil starts in-place renaming.
#ifndef SPDSX_PATCHEDIT_SOURCE_KIT_CHOOSER_H_
#define SPDSX_PATCHEDIT_SOURCE_KIT_CHOOSER_H_

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace spdsx {

class KitChooser : public juce::Component {
public:
  explicit KitChooser(int kit_count);

  // A different kit was picked (arrows or the dropdown menu).
  std::function<void(int)> on_select;
  // The user finished renaming (already trimmed and non-empty).
  std::function<void(const juce::String&)> on_rename;
  // Names the dropdown menu's entries; the parent supplies live names.
  std::function<juce::String(int)> kit_name;

  // Updates the display; the parent calls this whenever the current
  // kit or its name changes.
  void SetCurrent(int index, const juce::String& name);
  int current() const { return current_; }

  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseUp(const juce::MouseEvent& event) override;

private:
  void OpenMenu();
  void Step(int delta);

  int kit_count_;
  int current_ = 0;
  juce::String name_;
  juce::ShapeButton prev_;
  juce::ShapeButton next_;
  juce::ShapeButton pencil_;
  juce::Label name_label_;
  // The chip between the arrows: chrome painted by us, clicks open the
  // kit menu.
  juce::Rectangle<int> chip_;
  juce::Rectangle<int> number_area_;
  juce::Rectangle<int> triangle_area_;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_KIT_CHOOSER_H_
