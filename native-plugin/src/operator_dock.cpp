#include "operator_dock.hpp"
#include "operator_panel.hpp"
#include "presentation_document.hpp"
#include "pptbridge_registry.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <callback/calldata.h>
#include <callback/proc.h>
#include <QApplication>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <unordered_set>

namespace pptbridge {
namespace {
constexpr const char *kDockId = "pptbridge-operator-v1";
QPointer<OperatorPanel> panel;
std::atomic<bool> focused{false};
using SourceRef = std::unique_ptr<obs_source_t, decltype(&obs_source_release)>;

bool IsSlideSource(obs_source_t *source)
{
  return source && !obs_source_removed(source) && obs_source_get_id(source) &&
    std::strcmp(obs_source_get_id(source), "pptbridge_slide_source") == 0;
}

std::shared_ptr<PresentationDocument> GetDocument(obs_source_t *source)
{
  if (!IsSlideSource(source)) return {};
  obs_data_t *settings = obs_source_get_settings(source);
  std::string path = settings ? obs_data_get_string(settings, "pptx_path") : "";
  if (settings) obs_data_release(settings);
  return path.empty() ? nullptr : Registry::Instance().Acquire(path);
}

struct ProgramSearch {
  std::shared_ptr<PresentationDocument> document;
  std::unordered_set<obs_source_t *> visited;
  bool found = false;
};

bool VisitProgram(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
  auto &search = *static_cast<ProgramSearch *>(param);
  if (search.found || !item || !obs_sceneitem_visible(item)) return !search.found;
  auto *source = obs_sceneitem_get_source(item);
  if (GetDocument(source) == search.document) { search.found = true; return false; }
  auto *scene = obs_source_is_group(source) ? obs_group_from_source(source) : obs_scene_from_source(source);
  if (scene && search.visited.insert(source).second)
    obs_scene_enum_items(scene, VisitProgram, &search);
  return !search.found;
}

bool IsInProgram(const std::shared_ptr<PresentationDocument> &document)
{
  SourceRef root(obs_frontend_get_current_scene(), obs_source_release);
  if (!root) return false;
  auto *scene = obs_source_is_group(root.get()) ? obs_group_from_source(root.get()) : obs_scene_from_source(root.get());
  ProgramSearch search{document, {}, false};
  search.visited.insert(root.get());
  if (scene) obs_scene_enum_items(scene, VisitProgram, &search);
  return search.found;
}

class ObsBackend final : public OperatorBackend {
public:
  std::vector<OperatorSource> Sources() override
  {
    std::vector<OperatorSource> sources;
    obs_enum_sources([](void *data, obs_source_t *source) {
      if (IsSlideSource(source)) {
        auto &items = *static_cast<std::vector<OperatorSource> *>(data);
        const char *uuid = obs_source_get_uuid(source);
        const char *name = obs_source_get_name(source);
        if (uuid) items.push_back({uuid, name ? name : "PPTBridge Slide"});
      }
      return true;
    }, &sources);
    std::sort(sources.begin(), sources.end(), [](const auto &a, const auto &b) {
      return a.name < b.name || (a.name == b.name && a.id < b.id);
    });
    return sources;
  }

  OperatorDeck Read(const std::string &id) override
  {
    SourceRef source(obs_get_source_by_uuid(id.c_str()), obs_source_release);
    auto document = GetDocument(source.get());
    if (!document) return {};
    auto deck = document->OperatorSnapshot();
    deck.identity = id + "\n" + deck.identity;
    deck.in_program = IsInProgram(document);
    deck.capture_status = "Capture status unavailable; check OBS Program";
    calldata_t data;
    calldata_init(&data);
    auto *handler = obs_source_get_proc_handler(source.get());
    if (handler && proc_handler_call(handler, "pptbridge_operator_status", &data)) {
      const char *status = calldata_string(&data, "status");
      if (status) deck.capture_status = status;
    }
    calldata_free(&data);
    // Same throttled, single-inflight status path as the source. This does not
    // move the presentation or export additional slides.
    if (deck.live_enabled) document->SyncLiveStateAsync();
    return deck;
  }

  bool Execute(const std::string &id, const std::string &expected,
               OperatorCommand command, std::size_t index, std::string &error) override
  {
    SourceRef source(obs_get_source_by_uuid(id.c_str()), obs_source_release);
    auto document = GetDocument(source.get());
    if (!document) { error = "The source was removed or its presentation is unavailable."; return false; }
    auto deck = document->OperatorSnapshot();
    deck.identity = id + "\n" + deck.identity;
    if (expected.empty() || deck.identity != expected) {
      error = "The deck changed. Select a slide again."; return false;
    }
    if (command == OperatorCommand::Black) {
      if (!deck.valid || deck.loading) { error = "Presentation is not ready."; return false; }
      document->ToggleBlackScreen();
      return true;
    }
    if (!OperatorCanNavigate(deck)) {
      error = deck.busy ? "Wait for the current PowerPoint command to finish." :
        "Presentation is loading or Live Mode is not ready. Use the source properties to start/recover it.";
      return false;
    }
    if (command == OperatorCommand::Show && index >= deck.slides.size()) {
      error = "The selected slide no longer exists."; return false;
    }
    switch (command) {
    case OperatorCommand::Show: document->GoTo(index); break;
    case OperatorCommand::Next: document->Next(); break;
    case OperatorCommand::Previous: document->Previous(); break;
    case OperatorCommand::Black: break;
    }
    return true;
  }
};
}

bool OperatorDockHasFocus() { return focused.load(std::memory_order_relaxed); }

void RegisterOperatorDock()
{
  if (panel || !qApp || QThread::currentThread() != qApp->thread()) return;
  auto *widget = new OperatorPanel(std::make_unique<ObsBackend>());
  if (!obs_frontend_add_dock_by_id(kDockId, "PPTBridge Control", widget)) {
    delete widget;
    blog(LOG_WARNING, "[PPTBridge SK] Could not register operator dock");
    return;
  }
  panel = widget;
  QObject::connect(qApp, &QApplication::focusChanged, widget, [widget](QWidget *, QWidget *now) {
    focused.store(now && (now == widget || widget->isAncestorOf(now)), std::memory_order_relaxed);
  });
  QObject::connect(widget, &QObject::destroyed, qApp, [] {
    focused.store(false, std::memory_order_relaxed);
  });
  blog(LOG_INFO, "[PPTBridge SK] Operator dock v1 registered (build %s)", PPTBRIDGE_BUILD_ID);
}

void UnregisterOperatorDock()
{
  focused.store(false, std::memory_order_relaxed);
  if (panel) {
    auto remaining = panel;
    obs_frontend_remove_dock(kDockId);
    // remove_dock normally owns/deletes the widget. QPointer prevents a double
    // delete; explicitly finish any deferred deletion before module unload.
    if (remaining) delete remaining.data();
  }
  panel.clear();
}
} // namespace pptbridge
