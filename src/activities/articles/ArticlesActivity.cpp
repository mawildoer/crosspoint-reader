#include "ArticlesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <PhoneSync.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

ArticlesActivity::ArticlesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Articles", renderer, mappedInput) {}

void ArticlesActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
}

void ArticlesActivity::reload() {
  RenderLock lock;
  loadedGeneration_ = phonesync::storeGeneration();
  phonesync::store::loadNewest(articles_, MAX_ARTICLES);
  labels_.clear();
  labels_.reserve(articles_.size());
  for (const auto& article : articles_) {
    labels_.push_back((article.read ? "" : "\xE2\x80\xA2 ") + article.title);
  }
  rowItems_.assign(articles_.size() + 1, fui::ListItem{});
  for (size_t i = 0; i < articles_.size(); i++) {
    rowItems_[i].label = labels_[i].c_str();
    rowItems_[i].value = articles_[i].source.c_str();
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
  auto& last = rowItems_.back();
  last.label = tr(STR_PAPERBOY_PAIR_PHONE);
  last.actionValue = static_cast<int16_t>(articles_.size());
  if (nav.selected >= listCount()) nav.reset(0);
}

int ArticlesActivity::listCount() const { return static_cast<int>(rowItems_.size()); }

const char* ArticlesActivity::headerTitle() const {
  const auto status = phonesync::status();
  title_ = tr(STR_PAPERBOY_ARTICLES);
  if (status.receiving) {
    title_ += " - ";
    title_ += tr(STR_PAPERBOY_RECEIVING);
  } else if (status.linked) {
    title_ += " - ";
    title_ += tr(STR_PAPERBOY_CONNECTED);
  }
  return title_.c_str();
}

bool ArticlesActivity::handleCustomInput() {
  const bool linked = phonesync::status().linked;
  if (phonesync::storeGeneration() != loadedGeneration_) {
    reload();
    requestUpdate();
  } else if (linked != linkedShown_) {
    requestUpdate();
  }
  linkedShown_ = linked;
  return false;
}

void ArticlesActivity::onBackButton() { onGoHome(); }

void ArticlesActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  if (index == static_cast<int>(articles_.size())) {
    phonesync::forgetPhone();
    requestUpdate();
    return;
  }
  app.clearTapFlash();
  onSelectBook(phonesync::store::pathFor(articles_[index].id));
}

void ArticlesActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}
