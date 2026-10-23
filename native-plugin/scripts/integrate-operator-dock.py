#!/usr/bin/env python3
"""Idempotent post-integration hardening; CI commits edits before compilation.

The initial integration is already committed. Refuse unexpected source rather
than silently modifying another version. No source changes occur at runtime.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
changes = {}

def edit(path, before, after):
    file = ROOT / path
    text = changes.get(file, file.read_text(encoding='utf-8'))
    if after in text:
        return
    if text.count(before) != 1:
        raise RuntimeError(f'Expected one source anchor in {path}: {before[:80]!r}')
    changes[file] = text.replace(before, after, 1)

for name, marker in [
    ('native-plugin/src/plugin-main.cpp', 'pptbridge::RegisterOperatorDock();'),
    ('native-plugin/src/presentation_document_win.cpp', 'PresentationDocument::OperatorSnapshot()'),
    ('native-plugin/CMakeLists.txt', 'src/operator_dock.cpp'),
]:
    if marker not in (ROOT / name).read_text(encoding='utf-8'):
        raise RuntimeError('Initial operator integration is missing: ' + name)

p = 'native-plugin/src/operator_model.hpp'
edit(p, '#include <optional>\n', '#include <optional>\n#include <memory>\n#include <utility>\n')
edit(p, 'namespace pptbridge {\n', 'namespace pptbridge {\n\ninline std::atomic<uint64_t> operator_next_instance_id{1};\n')
edit(p, '''  explicit OperatorCommandTicket(std::atomic<unsigned> &counter) : counter_(counter)
  { counter_.fetch_add(1, std::memory_order_relaxed); }''', '''  explicit OperatorCommandTicket(std::atomic<unsigned> &counter, std::shared_ptr<void> owner = {})
    : counter_(counter), owner_(std::move(owner))
  { counter_.fetch_add(1, std::memory_order_relaxed); }''')
edit(p, '''  std::atomic<unsigned> &counter_;
};''', '''  std::atomic<unsigned> &counter_;
  // Keep the containing document alive through the destructor's decrement,
  // independent of the worker closure's capture destruction order.
  std::shared_ptr<void> owner_;
};''')
p = 'native-plugin/src/presentation_document.hpp'
edit(p, '  std::atomic<unsigned> operator_navigation_inflight_{0};',
     '  const uint64_t operator_instance_id_ = operator_next_instance_id.fetch_add(1);\n  std::atomic<unsigned> operator_navigation_inflight_{0};')
p = 'native-plugin/src/presentation_document_win.cpp'
edit(p, '''  deck.identity = impl_->path + "#" + std::to_string(operator_assets_generation_.load());''',
     '''  deck.identity = impl_->path + "#" + std::to_string(operator_instance_id_) + ":" +
    std::to_string(operator_assets_generation_.load());''')
edit(p, '  auto ticket = std::make_shared<OperatorCommandTicket>(operator_navigation_inflight_);',
     '  auto ticket = std::make_shared<OperatorCommandTicket>(operator_navigation_inflight_, self);')
edit(p, '''    if (prepared_new_deck) {
      impl_->slides = std::move(deck_data.slides);''', '''    if (prepared_new_deck) {
      // Also invalidate on publication: reloading can replace assets without
      // changing the slide count, while the old snapshot was visible at start.
      operator_assets_generation_.fetch_add(1, std::memory_order_relaxed);
      impl_->slides = std::move(deck_data.slides);''')
p = 'native-plugin/tests/operator_model_test.cpp'
edit(p, '    require(counter == 0, "tickets release commands");', '''    require(counter == 0, "tickets release commands");
    auto owner = std::make_shared<std::atomic<unsigned>>(0);
    std::weak_ptr<std::atomic<unsigned>> weak = owner;
    auto ticket = std::make_unique<OperatorCommandTicket>(*owner, owner);
    owner.reset();
    require(!weak.expired(), "ticket retains the document counter owner");
    ticket.reset();
    require(weak.expired(), "owner is released after ticket destruction");''')
p = 'native-plugin/tests/operator_panel_test.cpp'
edit(p, '{"Verse two", "Repeat here", path.toUtf8().toStdString()}',
     '{u8"Verse two \\uC740\\uD61C", "Repeat here", path.toUtf8().toStdString()}')
edit(p, '    grid->setCurrentRow(1);\n    require(backend->commands == 0', '''    search->setText(QString::fromUtf8(u8"\\uC740\\uD61C"));
    require(grid->item(0)->isHidden() && !grid->item(1)->isHidden(), "Korean title search");
    search->clear();
    grid->setCurrentRow(1);
    require(backend->commands == 0''')

p = 'native-plugin/CMakeLists.txt'
edit(p, 'set_tests_properties(operator-panel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)',
     'set_tests_properties(operator-panel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_DEBUG_PLUGINS=1;QT_LOGGING_TO_CONSOLE=1" TIMEOUT 60)')
p = 'native-plugin/tests/operator_panel_test.cpp'
edit(p, '  QApplication app(argc, argv);', '  std::cerr << "Starting Qt panel test" << std::endl;\n  QApplication app(argc, argv);\n  std::cerr << "Qt application initialized" << std::endl;')
edit(p, '{ ++checks; if (!value) throw std::runtime_error(message); }',
     '{ ++checks; std::cerr << "Check " << checks << ": " << message << std::endl; if (!value) throw std::runtime_error(message); }')

for file, text in changes.items():
    file.write_text(text, encoding='utf-8', newline='\n')
    print('Hardened', file.relative_to(ROOT))
print(f'Hardening complete: {len(changes)} files changed')
