#pragma once
#include <ArticleStore.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Articles delivered by the phone (lib/PhoneSync), newest first. Unread ones carry a dot.
// The last row removes the phone's bond so another phone can pair.
class ArticlesActivity final : public UiListActivity {
 public:
  explicit ArticlesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  bool handleCustomInput() override;
  void onBackButton() override;

  void reload();

  // The newest articles only: every title is held in RAM while the list is open.
  static constexpr size_t MAX_ARTICLES = 60;
  std::vector<phonesync::store::Entry> articles_;
  std::vector<std::string> labels_;
  std::vector<freeink::ui::ListItem> rowItems_;
  uint32_t loadedGeneration_ = 0;
  bool linkedShown_ = false;
  mutable std::string title_;
};
