#!/usr/bin/env python3
"""Idempotent reviewed layout fixes; CI commits edits before compilation.

Initial integration and lifetime/generation hardening are already committed.
Refuse unexpected source. No source changes occur at plugin runtime.
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

for path, marker in [
    ('native-plugin/src/presentation_document.hpp', 'operator_instance_id_'),
    ('native-plugin/src/operator_model.hpp', 'std::shared_ptr<void> owner_'),
    ('native-plugin/src/plugin-main.cpp', 'pptbridge::RegisterOperatorDock();'),
]:
    if marker not in (ROOT / path).read_text(encoding='utf-8'):
        raise RuntimeError('Required previous integration missing: ' + path)

p = 'native-plugin/src/operator_panel.cpp'
edit(p, '#include <QSignalBlocker>\n', '#include <QSignalBlocker>\n#include <QScrollArea>\n')
edit(p, '  grid_->setGridSize(QSize(184, 136));', '  grid_->setGridSize(QSize(184, 148));')
edit(p, '  grid_->setMinimumHeight(140);', '  grid_->setMinimumHeight(180);')
edit(p, '''  auto *previews = new QWidget(splitter);
  auto *preview_layout''', '''  auto *previews = new QWidget;
  auto *preview_scroll = new QScrollArea(splitter);
  preview_scroll->setWidgetResizable(true);
  preview_scroll->setFrameShape(QFrame::NoFrame);
  preview_scroll->setMinimumHeight(160);
  preview_scroll->setWidget(previews);
  auto *preview_layout''')
edit(p, '''  splitter->addWidget(previews);
  splitter->setStretchFactor(0, 3);''', '''  splitter->addWidget(preview_scroll);
  splitter->setChildrenCollapsible(false);
  splitter->setStretchFactor(0, 3);''')
edit(p, '''      auto *item = new QListWidgetItem(grid_);
      item->setToolTip''', '''      auto *item = new QListWidgetItem(grid_);
      // Fix the delegate's cell size before asynchronous icons arrive. An
      // initially empty item otherwise caches a short, clipped icon row.
      item->setSizeHint(QSize(180, 140));
      item->setToolTip''')
edit(p, '''      if (auto *image = images_.object(i)) item->setIcon(QIcon(*image));''', '''      if (item->icon().isNull()) {
        if (auto *image = images_.object(i)) item->setIcon(QIcon(*image));
      }''')
p = 'native-plugin/tests/operator_panel_test.cpp'
edit(p, '#include <QImage>\n', '#include <QImage>\n#include <QPainter>\n')
edit(p, '''    image.fill(Qt::darkBlue);
    const auto path''', '''    image.fill(Qt::darkBlue);
    {
      QPainter painter(&image);
      painter.setPen(Qt::white);
      QFont font = painter.font(); font.setPointSize(24); painter.setFont(font);
      painter.drawText(image.rect(), Qt::AlignCenter, "Slide preview\\nTest fixture - not PowerPoint");
    }
    const auto path''')
edit(p, '''    require(panel.grab().save("operator-panel-smoke.png"), "Qt panel screenshot saved");''', '''    require(grid->visualItemRect(grid->item(0)).height() >= 130,
            "thumbnail cell preserves readable image and caption height");
    require(!grid->item(0)->icon().isNull(), "visible thumbnail decoded and installed");
    require(grid->item(0)->text().contains("Verse one"), "thumbnail caption retained");
    require(panel.grab().save("operator-panel-smoke.png"), "Qt panel screenshot saved");''')

for file, text in changes.items():
    file.write_text(text, encoding='utf-8', newline='\n')
    print('Updated', file.relative_to(ROOT))
print(f'Layout integration complete: {len(changes)} files changed')
