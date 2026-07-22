// The feedback dialog, modeled on the one in ~/words: a hint line, three
// tall choices (report a bug / request a feature / general feedback) and
// the build id small in the footer. Choosing an option slides to a
// composer — your words, plus a verbatim preview of everything the
// report attaches — and Send posts it to the report relay, which files a
// GitHub issue; no GitHub account needed. If sending fails the report
// can be copied to the clipboard instead, so nothing typed is lost.
#ifndef SPDSX_PATCHEDIT_SOURCE_FEEDBACK_DIALOG_H_
#define SPDSX_PATCHEDIT_SOURCE_FEEDBACK_DIALOG_H_

#include <juce_gui_basics/juce_gui_basics.h>

#include "bug_report.h"

namespace spdsx {

class FeedbackPanel : public juce::Component {
public:
  // `seed` carries the environment the app collected (version, OS,
  // device, document); the panel fills in the category, the user's text
  // and the app log at send time.
  explicit FeedbackPanel(BugReport seed);

  void resized() override;

  // Builds the panel and launches it in an async modal dialog window.
  static void Show(BugReport seed);

private:
  enum class Page { kChoose, kCompose, kDone };

  void ChooseCategory(const juce::String& category,
                      const juce::String& prompt);
  void SendReport();
  void ShowPage(Page page);
  void CloseDialog();

  BugReport seed_;
  juce::String category_;

  // Page one: the three choices, words-style.
  juce::Label heading_ {{}, "Feedback"};
  juce::Label hint_;
  juce::TextButton report_bug_ {juce::String::fromUTF8("🐞 Report a Bug")};
  juce::TextButton request_feature_ {
      juce::String::fromUTF8("✨ Request a Feature")};
  juce::TextButton general_feedback_ {
      juce::String::fromUTF8("💬 General Feedback")};

  // Page two: the composer.
  juce::Label prompt_;
  juce::TextEditor text_;
  juce::Label attached_caption_ {{}, "Attached to the report:"};
  juce::TextEditor attached_;  // read-only preview of env + log
  juce::TextButton send_ {"Send"};
  juce::TextButton back_ {"Back"};

  // Page three: the outcome.
  juce::Label outcome_;
  juce::TextButton view_issue_ {"View on GitHub"};
  juce::TextButton copy_report_ {"Copy report"};
  juce::String issue_url_;

  // Footer, on every page.
  juce::Label build_;
  juce::TextButton cancel_ {"Cancel"};

  Page page_ = Page::kChoose;
  bool sending_ = false;
};

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_FEEDBACK_DIALOG_H_
