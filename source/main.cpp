#include <cstdio>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "commands.hpp"
#include "main_component.hpp"

namespace spdsx {

class MainMenu : public juce::MenuBarModel {
public:
  explicit MainMenu(juce::ApplicationCommandManager& commands)
      : commands_(commands)
  {
  }

  juce::StringArray getMenuBarNames() override
  {
    return {"File", "Edit", "View"};
  }

  juce::PopupMenu getMenuForIndex(int, const juce::String& name) override
  {
    juce::PopupMenu menu;
    if (name == "File") {
      menu.addCommandItem(&commands_, commands::kFileNew);
      menu.addCommandItem(&commands_, commands::kFileOpen);
      menu.addSeparator();
      menu.addCommandItem(&commands_, commands::kFileSave);
      menu.addCommandItem(&commands_, commands::kFileSaveAs);
    } else if (name == "Edit") {
      menu.addCommandItem(&commands_, commands::kUndo);
      menu.addCommandItem(&commands_, commands::kRedo);
    } else if (name == "View") {
      menu.addCommandItem(&commands_, commands::kToggleBrowser);
    }
    return menu;
  }

  void menuItemSelected(int, int) override {}

private:
  juce::ApplicationCommandManager& commands_;
};

class MainWindow : public juce::DocumentWindow {
public:
  // Takes ownership of content.
  explicit MainWindow(MainComponent* content)
      : juce::DocumentWindow("SPD-SX Patch Edit",
            juce::Colour(0xff12161b),
            juce::DocumentWindow::allButtons)
  {
    setUsingNativeTitleBar(true);
    setContentOwned(content, true);
    setResizable(true, true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
  }

  void closeButtonPressed() override
  {
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
  }

  // The grid handles the spacebar; hand it the keyboard whenever the
  // window becomes active.
  void activeWindowStatusChanged() override
  {
    if (isActiveWindow() && getContentComponent() != nullptr) {
      getContentComponent()->grabKeyboardFocus();
    }
  }
};

class App : public juce::JUCEApplication {
public:
  const juce::String getApplicationName() override
  {
    return "spdsx-patchedit";
  }
  const juce::String getApplicationVersion() override { return "0.1.0"; }

  void initialise(const juce::String&) override
  {
    content = new MainComponent(command_manager);
    command_manager.registerAllCommandsForTarget(content);
    command_manager.setFirstCommandTarget(content);

    // --load <slot> <wav> pre-fills a slot; useful for testing without
    // drag and drop. May be repeated. Slot indices are
    // (row * 3 + col) * 2, +1 for the bottom slot.
    auto args = getCommandLineParameterArray();
    for (int i = 0; i < args.size(); ++i) {
      if (args[i] == "--load" && i + 2 < args.size()) {
        content->LoadSample(args[i + 1].getIntValue(),
            juce::File::getCurrentWorkingDirectory().getChildFile(
                args[i + 2]));
        i += 2;
      } else {
        std::fprintf(stderr, "unrecognized argument '%s'\n"
                             "usage: spdsx-patchedit [--load <slot> <file.wav>]...\n",
            args[i].toRawUTF8());
      }
    }

    window = std::make_unique<MainWindow>(content);
    menu = std::make_unique<MainMenu>(command_manager);
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(menu.get());
#else
    window->setMenuBar(menu.get());
#endif
  }

  // Give the user a chance to save (or bail out) before quitting with
  // unsaved changes.
  void systemRequestedQuit() override
  {
    if (content == nullptr) {
      quit();
      return;
    }
    content->document().saveIfNeededAndUserAgreesAsync(
        [](juce::FileBasedDocument::SaveResult result)
        {
          if (result == juce::FileBasedDocument::savedOk) {
            juce::JUCEApplication::quit();
          }
        });
  }

  void shutdown() override
  {
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
#else
    if (window != nullptr) {
      window->setMenuBar(nullptr);
    }
#endif
    command_manager.setFirstCommandTarget(nullptr);
    content = nullptr;
    window.reset();
    menu.reset();
  }

private:
  juce::ApplicationCommandManager command_manager;
  std::unique_ptr<MainWindow> window;
  std::unique_ptr<MainMenu> menu;
  MainComponent* content = nullptr;  // owned by window
};

}  // namespace spdsx

START_JUCE_APPLICATION(spdsx::App)
