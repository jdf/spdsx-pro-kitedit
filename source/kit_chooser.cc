#include "kit_chooser.h"

namespace spdsx {

namespace {

const juce::Colour kText(0xffe6edf5);
const juce::Colour kDim(0xff8a97a6);
const juce::Colour kChipBg(0xff161b22);
const juce::Colour kChipBorder(0xff242d38);

juce::Path ArrowPath(bool pointing_right)
{
  juce::Path p;
  if (pointing_right) {
    p.addTriangle(0.0f, 0.0f, 0.0f, 1.0f, 0.75f, 0.5f);
  } else {
    p.addTriangle(0.75f, 0.0f, 0.75f, 1.0f, 0.0f, 0.5f);
  }
  return p;
}

juce::Path PencilPath()
{
  juce::Path p;
  // A diagonal body with a tip: enough to read as "edit" at 12px.
  p.addQuadrilateral(0.28f, 0.62f, 0.62f, 0.28f, 0.74f, 0.40f, 0.40f,
      0.74f);
  p.addTriangle(0.28f, 0.62f, 0.40f, 0.74f, 0.20f, 0.82f);
  return p;
}

}  // namespace

KitChooser::KitChooser(int kit_count)
    : kit_count_(kit_count)
    , prev_("prev-kit", kDim, kText, kText)
    , next_("next-kit", kDim, kText, kText)
    , pencil_("rename-kit", kDim, kText, kText)
{
  prev_.setShape(ArrowPath(false), false, true, false);
  next_.setShape(ArrowPath(true), false, true, false);
  pencil_.setShape(PencilPath(), false, true, false);
  prev_.onClick = [this] { Step(-1); };
  next_.onClick = [this] { Step(1); };
  pencil_.onClick = [this] { name_label_.showEditor(); };
  addAndMakeVisible(prev_);
  addAndMakeVisible(next_);
  addAndMakeVisible(pencil_);

  name_label_.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
  name_label_.setColour(juce::Label::textColourId, kText);
  name_label_.setJustificationType(juce::Justification::centredLeft);
  // Renaming starts from the pencil, never from a stray click; clicks
  // on the name open the kit menu like the rest of the chip.
  name_label_.setEditable(false, false, false);
  name_label_.onTextChange = [this]
  {
    const auto text = name_label_.getText().trim();
    if (text.isNotEmpty() && text != name_ && on_rename) {
      on_rename(text);
    } else {
      name_label_.setText(name_, juce::dontSendNotification);
    }
  };
  // Hand the keyboard back to the grid when editing ends, so the
  // trigger keys keep working.
  name_label_.onEditorHide = [this]
  {
    if (auto* parent = getParentComponent()) {
      parent->grabKeyboardFocus();
    }
  };
  addAndMakeVisible(name_label_);
  // The label sits inside the chip; its clicks reach us for the menu.
  name_label_.addMouseListener(this, false);
}

void KitChooser::SetCurrent(int index, const juce::String& name)
{
  current_ = index;
  name_ = name;
  name_label_.setText(name, juce::dontSendNotification);
  prev_.setEnabled(index > 0);
  next_.setEnabled(index < kit_count_ - 1);
  repaint();
}

void KitChooser::Step(int delta)
{
  const int target = current_ + delta;
  if (target >= 0 && target < kit_count_ && on_select) {
    on_select(target);
  }
}

void KitChooser::OpenMenu()
{
  juce::PopupMenu menu;
  for (int i = 0; i < kit_count_; ++i) {
    menu.addItem(i + 1,
        juce::String(i + 1) + "  "
            + (kit_name ? kit_name(i) : juce::String()),
        true, i == current_);
  }
  menu.showMenuAsync(
      juce::PopupMenu::Options().withTargetComponent(this),
      [this](int result)
      {
        if (result > 0 && result - 1 != current_ && on_select) {
          on_select(result - 1);
        }
      });
}

void KitChooser::mouseUp(const juce::MouseEvent& event)
{
  const auto e = event.getEventRelativeTo(this);
  if (chip_.contains(e.getPosition()) && !name_label_.isBeingEdited()) {
    OpenMenu();
  }
}

void KitChooser::paint(juce::Graphics& g)
{
  g.setColour(kChipBg);
  g.fillRoundedRectangle(chip_.toFloat(), 6.0f);
  g.setColour(kChipBorder);
  g.drawRoundedRectangle(chip_.toFloat().reduced(0.5f), 6.0f, 1.0f);
  g.setColour(kDim);
  g.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
  g.drawText(juce::String(current_ + 1), number_area_,
      juce::Justification::centred);
  const auto tri = triangle_area_.toFloat();
  juce::Path p;
  p.addTriangle(tri.getX(), tri.getY(), tri.getRight(), tri.getY(),
      tri.getCentreX(), tri.getBottom());
  g.fillPath(p);
}

void KitChooser::resized()
{
  auto area = getLocalBounds();
  prev_.setBounds(area.removeFromLeft(22).reduced(3, 6));
  next_.setBounds(area.removeFromRight(22).reduced(3, 6));
  area.removeFromLeft(6);
  area.removeFromRight(6);
  chip_ = area;
  auto inner = area.reduced(8, 2);
  number_area_ = inner.removeFromLeft(28);
  triangle_area_ =
      inner.removeFromRight(16).withSizeKeepingCentre(10, 6);
  pencil_.setBounds(inner.removeFromLeft(22).reduced(1));
  inner.removeFromLeft(2);
  name_label_.setBounds(inner);
}

}  // namespace spdsx
