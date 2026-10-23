#!/usr/bin/env python3
"""Apply the reviewed integration edits once, refusing ambiguous source anchors.

The restricted integration CI job commits these changes before the read-only
Windows build checks out its exact resulting SHA. Re-running is a no-op.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
changes = {}

def edit(path, before, after):
    file = ROOT / path
    text = changes.get(file, file.read_text(encoding="utf-8"))
    if after in text:
        return
    if text.count(before) != 1:
        raise RuntimeError(f"Expected one unchanged anchor in {path}: {before[:80]!r}")
    changes[file] = text.replace(before, after, 1)

h = "native-plugin/src/presentation_document.hpp"
edit(h, "#include <cstddef>\n", '#include <cstddef>\n#ifdef _WIN32\n#include "operator_model.hpp"\n#endif\n')
edit(h, "  PresentationStatus SnapshotStatus() const;\n", "  PresentationStatus SnapshotStatus() const;\n#ifdef _WIN32\n  OperatorDeck OperatorSnapshot() const;\n#endif\n")
edit(h, "  std::unique_ptr<Impl> impl_;\n", "  std::unique_ptr<Impl> impl_;\n#ifdef _WIN32\n  std::atomic<unsigned> operator_navigation_inflight_{0};\n  std::atomic<uint64_t> operator_assets_generation_{0};\n#endif\n")
p = "native-plugin/src/presentation_document_win.cpp"
edit(p, "bool PresentationDocument::SetCueChecked(std::size_t index, bool checked)\n", '''OperatorDeck PresentationDocument::OperatorSnapshot() const
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  OperatorDeck deck;
  deck.name = impl_->name;
  deck.path = impl_->path;
  deck.identity = impl_->path + "#" + std::to_string(operator_assets_generation_.load());
  deck.loading = impl_->loading;
  deck.valid = impl_->loaded && !impl_->slides.empty();
  deck.busy = operator_navigation_inflight_.load(std::memory_order_relaxed) != 0;
  deck.live_enabled = impl_->live_enabled;
  deck.live_ready = impl_->live_ready;
  deck.black = impl_->black_screen;
  deck.error = impl_->last_error;
  deck.current = impl_->slides.empty() ? 0 : std::min(impl_->current_index, impl_->slides.size() - 1);
  deck.slides.reserve(impl_->slides.size());
  for (std::size_t index = 0; index < impl_->slides.size(); ++index) {
    const auto &slide = impl_->slides[index];
    deck.slides.push_back({CueTitleForSlide(impl_->slides, index),
                          slide.meta.notes, WideToUtf8(slide.image_path)});
  }
  return deck;
}

bool PresentationDocument::SetCueChecked(std::size_t index, bool checked)
''')
edit(p, '''  auto self = shared_from_this();
  std::thread([self, mode, target_slide, request_generation]() {''', '''  auto self = shared_from_this();
  auto ticket = std::make_shared<OperatorCommandTicket>(operator_navigation_inflight_);
  std::thread([self, ticket, mode, target_slide, request_generation]() {''')
edit(p, '''    impl_->loading = true;
    impl_->load_requested = false;
    impl_->active_force_reload = impl_->force_reload;''', '''    impl_->loading = true;
    operator_assets_generation_.fetch_add(1, std::memory_order_relaxed);
    impl_->load_requested = false;
    impl_->active_force_reload = impl_->force_reload;''')
p = "native-plugin/src/plugin-main.cpp"
edit(p, '#include "presentation_document.hpp"\n', '#include "presentation_document.hpp"\n#include "operator_dock.hpp"\n')
edit(p, '''  if (!obs_application_is_active()) {
    return;
  }

  dispatch_action_to_documents''', '''  if (!obs_application_is_active() || pptbridge::OperatorDockHasFocus()) {
    return;
  }

  dispatch_action_to_documents''')
edit(p, '''    apply_default_hotkeys_if_needed();
    load_osc_settings_from_config();
  }
}''', '''    apply_default_hotkeys_if_needed();
    load_osc_settings_from_config();
    pptbridge::RegisterOperatorDock();
  }
  if (event == OBS_FRONTEND_EVENT_EXIT) {
    pptbridge::UnregisterOperatorDock();
  }
}''')
edit(p, '''void obs_module_unload(void)
{
  set_clicker_capture_enabled(false);''', '''void obs_module_unload(void)
{
  pptbridge::UnregisterOperatorDock();
  set_clicker_capture_enabled(false);''')
p = "native-plugin/src/source_slide_win.cpp"
edit(p, "bool control_previous(obs_properties_t *, obs_property_t *, void *data)\n", '''void operator_capture_status(void *data, calldata_t *result)
{
  auto *context = static_cast<SourceContext *>(data);
  if (!context || !result) return;
  std::lock_guard<std::mutex> lock(context->render_state_mutex);
  const char *status = "No presentation";
  if (context->document) {
    if (context->document->IsBlackScreen()) {
      status = "BLACKOUT";
    } else if (!context->use_live_powerpoint || !context->document->IsLivePowerPointEnabled()) {
      status = "Cached slide mode";
    } else if (context->live_held_frame_valid && context->live_fallback_reported) {
      const auto age = std::chrono::steady_clock::now() - context->live_held_frame_at;
      status = age <= std::chrono::milliseconds(2500)
        ? "HOLDING previous live frame - capture interrupted"
        : "Live capture unavailable - hold expired / recovery";
    } else if (snapshot_live_child_status(context).capture_renderable) {
      status = "Live capture attached (verify OBS Program)";
    } else if (context->live_capture_suppressed_after_stop) {
      status = "Live Mode stopped";
    } else {
      status = "Live capture not ready - cached startup / recovery";
    }
  }
  calldata_set_string(result, "status", status);
}

bool control_previous(obs_properties_t *, obs_property_t *, void *data)
''')
edit(p, '''  context->mode = ViewMode::Slide;
  source_update(context, settings);
  return context;''', '''  context->mode = ViewMode::Slide;
  source_update(context, settings);
  proc_handler_add(obs_source_get_proc_handler(source),
                   "void pptbridge_operator_status(out string status)",
                   operator_capture_status, context);
  return context;''')
# A long outage should not expose an arbitrary lower source in the scene.
edit(p, '''      // Long outage: reveal the scene underneath (normally black or a safe
      // holding background), NOT an out-of-date title/first slide. Automatic
      // recovery remains the existing watchdog's responsibility.
      return;''', '''      // Long outage: draw an opaque safety frame, never reveal unrelated
      // sources below the presentation. The existing watchdog owns recovery.
      gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
      if (solid) {
        struct vec4 black = {};
        black.w = 1.0f;
        gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &black);
        while (gs_effect_loop(solid, "Solid")) {
          gs_draw_sprite(nullptr, 0, context->width, context->height);
        }
      }
      return;''')
p = "native-plugin/CMakeLists.txt"
# Append once; existing Windows backend and all macOS targets remain unchanged.
file = ROOT / p
text = file.read_text(encoding="utf-8")
marker = "# PPTBridge Operator Dock v1"
addition = '''
# PPTBridge Operator Dock v1
if(WIN32)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
  target_sources(pptbridge-obs PRIVATE src/operator_dock.cpp src/operator_panel.cpp)
  target_link_libraries(pptbridge-obs PRIVATE Qt6::Widgets)
  set(PPTBRIDGE_BUILD_ID "local-operator-dock" CACHE STRING "Source revision for diagnostics")
  target_compile_definitions(pptbridge-obs PRIVATE PPTBRIDGE_BUILD_ID="${PPTBRIDGE_BUILD_ID}")
  if(MSVC)
    target_compile_options(pptbridge-obs PRIVATE /utf-8)
  endif()
  include(CTest)
  if(BUILD_TESTING)
    add_executable(pptbridge-operator-model-test tests/operator_model_test.cpp)
    target_include_directories(pptbridge-operator-model-test PRIVATE src)
    add_test(NAME operator-model COMMAND pptbridge-operator-model-test)
    add_executable(pptbridge-operator-panel-test tests/operator_panel_test.cpp src/operator_panel.cpp)
    target_include_directories(pptbridge-operator-panel-test PRIVATE src)
    target_link_libraries(pptbridge-operator-panel-test PRIVATE Qt6::Widgets)
    target_compile_definitions(pptbridge-operator-panel-test PRIVATE NOMINMAX)
    if(MSVC)
      target_compile_options(pptbridge-operator-panel-test PRIVATE /utf-8)
    endif()
    add_test(NAME operator-panel COMMAND pptbridge-operator-panel-test)
    set_tests_properties(operator-panel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
  endif()
endif()
'''
if marker not in text:
    changes[file] = text.rstrip() + "\n" + addition
elif addition.strip() not in text:
    raise RuntimeError("Unexpected existing operator dock CMake integration")

# All anchors are validated before any source file is changed.
for file, text in changes.items():
    file.write_text(text, encoding="utf-8", newline="\n")
    print("Integrated", file.relative_to(ROOT))
print(f"Integration complete: {len(changes)} files changed")
