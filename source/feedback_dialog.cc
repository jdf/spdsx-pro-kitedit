#include "feedback_dialog.h"

#include <cstdlib>
#include <thread>

#include "app_log.h"

namespace spdsx {

namespace {

constexpr int kPanelWidth = 420;
constexpr int kPadding = 16;
constexpr int kRowHeight = 28;
constexpr int kOptionHeight = 36;
constexpr int kRowGap = 8;

// Where reports go. The relay (report-relay/ in this repo) holds the
// GitHub token and files the issue. SPDSX_REPORT_URL overrides for
// testing against a local `vercel dev`.
juce::String ReportEndpoint() {
  if (const char* url = std::getenv("SPDSX_REPORT_URL")) {
    return url;
  }
  return "https://spdsx-patchedit-report.vercel.app/api/v1/report";
}

juce::Colour DimText() {
  return juce::Colour(0xff8a8a96);  // the words dialog's hint grey
}

}  // namespace

FeedbackPanel::FeedbackPanel(BugReport seed)
    : seed_(std::move(seed)) {
  heading_.setFont(juce::FontOptions(18.0f, juce::Font::bold));
  addAndMakeVisible(heading_);

  hint_.setText("Your report goes straight to the developer — no account "
                "needed. Your platform, app build, and recent device "
                "activity ride along.",
                juce::dontSendNotification);
  hint_.setColour(juce::Label::textColourId, DimText());
  hint_.setFont(juce::FontOptions(13.0f));
  addAndMakeVisible(hint_);

  report_bug_.onClick = [this] {
    ChooseCategory("bug", "What went wrong? What did you expect instead?");
  };
  request_feature_.onClick = [this] {
    ChooseCategory("feature", "What should the app be able to do?");
  };
  general_feedback_.onClick = [this] {
    ChooseCategory("feedback", "What's on your mind?");
  };
  for (juce::TextButton* option :
       {&report_bug_, &request_feature_, &general_feedback_}) {
    option->setConnectedEdges(0);
    addAndMakeVisible(*option);
  }

  prompt_.setFont(juce::FontOptions(13.0f));
  addAndMakeVisible(prompt_);
  text_.setMultiLine(true, true);
  text_.setReturnKeyStartsNewLine(true);
  addAndMakeVisible(text_);

  attached_caption_.setColour(juce::Label::textColourId, DimText());
  attached_caption_.setFont(juce::FontOptions(12.0f));
  addAndMakeVisible(attached_caption_);
  attached_.setMultiLine(true);
  attached_.setReadOnly(true);
  attached_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                      11.0f,
                                      juce::Font::plain));
  attached_.setColour(juce::TextEditor::textColourId, DimText());
  addAndMakeVisible(attached_);

  send_.onClick = [this] { SendReport(); };
  addAndMakeVisible(send_);
  back_.onClick = [this] { ShowPage(Page::kChoose); };
  addAndMakeVisible(back_);

  outcome_.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(outcome_);
  view_issue_.onClick = [this] {
    juce::URL(issue_url_).launchInDefaultBrowser();
  };
  addAndMakeVisible(view_issue_);
  copy_report_.onClick = [this] {
    juce::SystemClipboard::copyTextToClipboard(
        text_.getText() + "\n\n" + attached_.getText());
    copy_report_.setButtonText("Copied");
  };
  addAndMakeVisible(copy_report_);

  build_.setText("spdsx-patchedit " + seed_.app_version,
                 juce::dontSendNotification);
  build_.setColour(juce::Label::textColourId, DimText());
  build_.setFont(juce::FontOptions(12.0f));
  addAndMakeVisible(build_);
  cancel_.onClick = [this] { CloseDialog(); };
  addAndMakeVisible(cancel_);

  ShowPage(Page::kChoose);
  setSize(kPanelWidth, 0);  // height set per page
}

void FeedbackPanel::ChooseCategory(const juce::String& category,
                                   const juce::String& prompt) {
  category_ = category;
  prompt_.setText(prompt, juce::dontSendNotification);
  // The attachment preview is prepared when the page opens (as the words
  // dialog does), so the user reads exactly what will be sent.
  seed_.log = AppLog::Recent();
  attached_.setText(EnvironmentReport(seed_) + "\nrecent activity:\n"
                        + (seed_.log.isEmpty() ? "(none)\n" : seed_.log),
                    juce::dontSendNotification);
  ShowPage(Page::kCompose);
  text_.grabKeyboardFocus();
}

void FeedbackPanel::SendReport() {
  if (sending_ || text_.getText().trim().isEmpty()) {
    return;
  }
  sending_ = true;
  send_.setButtonText("Sending…");
  send_.setEnabled(false);
  back_.setEnabled(false);

  BugReport report = seed_;
  report.category = category_;
  report.text = text_.getText();
  const juce::String body = ReportJson(report);
  juce::Component::SafePointer<FeedbackPanel> safe(this);
  std::thread([safe, body] {
    int status = 0;
    juce::String reply;
    {
      const juce::URL url =
          juce::URL(ReportEndpoint()).withPOSTData(body);
      const auto options =
          juce::URL::InputStreamOptions(
              juce::URL::ParameterHandling::inPostData)
              .withExtraHeaders("Content-Type: application/json")
              .withConnectionTimeoutMs(10000)
              .withStatusCode(&status);
      if (const auto stream = url.createInputStream(options)) {
        reply = stream->readEntireStreamAsString();
      }
    }
    juce::MessageManager::callAsync([safe, status, reply] {
      if (safe == nullptr) {
        return;
      }
      safe->sending_ = false;
      const juce::var parsed = juce::JSON::parse(reply);
      if (status == 201 && parsed.isObject()) {
        safe->issue_url_ = parsed["url"].toString();
        safe->outcome_.setText(
            "Reported as #" + parsed["number"].toString() + ". Thank you!",
            juce::dontSendNotification);
        safe->view_issue_.setVisible(safe->issue_url_.isNotEmpty());
        AppLog::Note("feedback sent: #" + parsed["number"].toString());
      } else {
        safe->issue_url_.clear();
        safe->outcome_.setText(
            "Couldn't send the report (status "
                + (status > 0 ? juce::String(status) : juce::String("none"))
                + "). Copy it and send it another way?",
            juce::dontSendNotification);
        safe->view_issue_.setVisible(false);
        AppLog::Note("feedback send failed, status " + juce::String(status));
      }
      safe->ShowPage(Page::kDone);
    });
  }).detach();
}

void FeedbackPanel::ShowPage(Page page) {
  page_ = page;
  const bool choose = page == Page::kChoose;
  const bool compose = page == Page::kCompose;
  const bool done = page == Page::kDone;

  hint_.setVisible(choose);
  for (juce::TextButton* option :
       {&report_bug_, &request_feature_, &general_feedback_}) {
    option->setVisible(choose);
  }
  prompt_.setVisible(compose);
  text_.setVisible(compose);
  attached_caption_.setVisible(compose);
  attached_.setVisible(compose);
  send_.setVisible(compose);
  back_.setVisible(compose);
  if (compose) {
    send_.setButtonText("Send");
    send_.setEnabled(true);
    back_.setEnabled(true);
  }
  outcome_.setVisible(done);
  view_issue_.setVisible(done && issue_url_.isNotEmpty());
  copy_report_.setVisible(done && issue_url_.isEmpty());
  cancel_.setButtonText(done ? "Close" : "Cancel");

  const int height = choose
      ? kPadding * 2 + kRowHeight + 44 + 3 * (kOptionHeight + kRowGap)
          + kRowHeight
      : compose ? 480
                : kPadding * 2 + kRowHeight + 3 * kRowHeight;
  setSize(kPanelWidth, height);
  if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>()) {
    dialog->setContentComponentSize(kPanelWidth, height);
  }
  resized();
}

void FeedbackPanel::resized() {
  auto area = getLocalBounds().reduced(kPadding);
  auto footer = area.removeFromBottom(kRowHeight);
  cancel_.setBounds(footer.removeFromRight(90));
  footer.removeFromRight(8);
  build_.setBounds(footer);
  area.removeFromBottom(kRowGap);

  heading_.setBounds(area.removeFromTop(kRowHeight));
  if (page_ == Page::kChoose) {
    hint_.setBounds(area.removeFromTop(44));
    for (juce::TextButton* option :
         {&report_bug_, &request_feature_, &general_feedback_}) {
      area.removeFromTop(kRowGap);
      option->setBounds(area.removeFromTop(kOptionHeight));
    }
  } else if (page_ == Page::kCompose) {
    prompt_.setBounds(area.removeFromTop(kRowHeight));
    area.removeFromTop(kRowGap);
    auto buttons = area.removeFromBottom(kRowHeight);
    send_.setBounds(buttons.removeFromRight(90));
    buttons.removeFromRight(8);
    back_.setBounds(buttons.removeFromRight(90));
    area.removeFromBottom(kRowGap);
    attached_.setBounds(area.removeFromBottom(150));
    attached_caption_.setBounds(area.removeFromBottom(20));
    area.removeFromBottom(kRowGap);
    text_.setBounds(area);
  } else {
    area.removeFromTop(kRowGap);
    outcome_.setBounds(area.removeFromTop(kRowHeight * 2));
    auto action = area.removeFromTop(kRowHeight);
    view_issue_.setBounds(action.withSizeKeepingCentre(160, kRowHeight));
    copy_report_.setBounds(action.withSizeKeepingCentre(160, kRowHeight));
  }
}

void FeedbackPanel::CloseDialog() {
  if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>()) {
    dialog->exitModalState(0);
  }
}

void FeedbackPanel::Show(BugReport seed) {
  auto panel = std::make_unique<FeedbackPanel>(std::move(seed));

  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(panel.release());
  options.dialogTitle = "Feedback";
  options.dialogBackgroundColour = juce::Colour(0xff2b2b2b);
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = false;
  options.launchAsync();
}

}  // namespace spdsx
