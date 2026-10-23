#include "operator_panel.hpp"
#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRunnable>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace pptbridge {
namespace {
QString Utf8(const std::string &s) { return QString::fromUtf8(s.data(), int(s.size())); }
QLabel *TextLabel(const char *name, QWidget *parent)
{
  auto *label = new QLabel(parent);
  label->setObjectName(name);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  return label;
}
QLabel *ImageLabel(QWidget *parent, int height)
{
  auto *label = TextLabel("cachedSlideImage", parent);
  label->setAlignment(Qt::AlignCenter);
  label->setMinimumSize(100, height);
  label->setMaximumHeight(height + 45);
  label->setText("No cached preview");
  return label;
}
}

OperatorPanel::OperatorPanel(std::unique_ptr<OperatorBackend> backend, QWidget *parent)
  : QWidget(parent), backend_(std::move(backend))
{
  setObjectName("pptbridgeOperatorPanel");
  setMinimumSize(340, 420);
  auto *layout = new QVBoxLayout(this);
  auto *help = TextLabel("operatorHelp", this);
  help->setText("Browse privately. Only Show selected changes the slideshow. "
                "Use OBS Program to verify the actual audience output.");
  layout->addWidget(help);
  sources_ = new QComboBox(this);
  sources_->setObjectName("deckSelector");
  sources_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  sources_->setMinimumContentsLength(18);
  sources_->addItem("Choose a PPTBridge Slide source...", QString());
  layout->addWidget(sources_);
  path_ = TextLabel("deckPath", this);
  layout->addWidget(path_);
  status_ = TextLabel("operatorStatus", this);
  status_->setAccessibleName("Presentation and capture status");
  layout->addWidget(status_);

  auto *splitter = new QSplitter(Qt::Vertical, this);
  auto *browser = new QWidget(splitter);
  auto *browser_layout = new QVBoxLayout(browser);
  browser_layout->setContentsMargins(0, 0, 0, 0);
  search_ = new QLineEdit(browser);
  search_->setObjectName("searchSlides");
  search_->setPlaceholderText("Search slide titles / notes (Korean or English)");
  search_->setClearButtonEnabled(true);
  browser_layout->addWidget(search_);
  grid_ = new QListWidget(browser);
  grid_->setObjectName("slideGrid");
  grid_->setViewMode(QListView::IconMode);
  grid_->setResizeMode(QListView::Adjust);
  grid_->setMovement(QListView::Static);
  grid_->setSelectionMode(QAbstractItemView::SingleSelection);
  grid_->setIconSize(QSize(160, 90));
  grid_->setGridSize(QSize(184, 136));
  grid_->setWordWrap(true);
  grid_->setUniformItemSizes(true);
  grid_->setMinimumHeight(140);
  browser_layout->addWidget(grid_);

  auto *previews = new QWidget(splitter);
  auto *preview_layout = new QGridLayout(previews);
  current_text_ = TextLabel("currentText", previews);
  next_text_ = TextLabel("nextText", previews);
  current_image_ = ImageLabel(previews, 85);
  next_image_ = ImageLabel(previews, 85);
  selected_text_ = TextLabel("selectedText", previews);
  selected_image_ = ImageLabel(previews, 135);
  notes_ = new QPlainTextEdit(previews);
  notes_->setObjectName("selectedNotes");
  notes_->setReadOnly(true);
  notes_->setPlaceholderText("Notes for the selected slide");
  notes_->setMaximumHeight(85);
  preview_layout->addWidget(current_text_, 0, 0);
  preview_layout->addWidget(next_text_, 0, 1);
  preview_layout->addWidget(current_image_, 1, 0);
  preview_layout->addWidget(next_image_, 1, 1);
  preview_layout->addWidget(selected_text_, 2, 0, 1, 2);
  preview_layout->addWidget(selected_image_, 3, 0, 1, 2);
  preview_layout->addWidget(notes_, 4, 0, 1, 2);
  splitter->addWidget(browser);
  splitter->addWidget(previews);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);
  layout->addWidget(splitter, 1);

  auto *buttons = new QGridLayout;
  auto button = [&](const char *text, const char *id, int row, int column, int span = 1) {
    auto *result = new QPushButton(text, this);
    result->setObjectName(id);
    result->setAutoDefault(false);
    result->setMinimumHeight(34);
    buttons->addWidget(result, row, column, 1, span);
    return result;
  };
  show_ = button("Show selected", "showSelected", 0, 0, 2);
  previous_ = button("Previous", "previousSlide", 1, 0);
  next_ = button("Next / animation", "nextSlide", 1, 1);
  back_ = button("Back to last shown", "lastShown", 2, 0);
  black_ = button("Blackout", "blackout", 2, 1);
  layout->addLayout(buttons);

  connect(sources_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
    last_message_.clear();
    search_->clear();
    Refresh();
  });
  connect(search_, &QLineEdit::textChanged, this, [this] { ApplyFilter(); });
  connect(grid_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (rebuilding_) return;
    if (row >= 0) selection_.Select(std::size_t(row));
    else selection_.ClearSelection();
    UpdateControls();
    UpdatePreviews();
  });
  connect(show_, &QPushButton::clicked, this, [this] {
    if (auto index = selection_.Selected()) Submit(OperatorCommand::Show, *index);
  });
  connect(previous_, &QPushButton::clicked, this, [this] { Submit(OperatorCommand::Previous); });
  connect(next_, &QPushButton::clicked, this, [this] { Submit(OperatorCommand::Next); });
  connect(back_, &QPushButton::clicked, this, [this] {
    if (auto index = selection_.Previous()) Submit(OperatorCommand::Show, *index);
  });
  connect(black_, &QPushButton::clicked, this, [this] { Submit(OperatorCommand::Black); });

  workers_.setMaxThreadCount(1);
  poll_ = new QTimer(this);
  connect(poll_, &QTimer::timeout, this, [this] { if (isVisible()) Refresh(); });
  poll_->start(250);
  thumbnails_ = new QTimer(this);
  connect(thumbnails_, &QTimer::timeout, this, [this] { if (isVisible()) PumpThumbnails(); });
  thumbnails_->start(100);
  RefreshSources();
  Refresh();
}

OperatorPanel::~OperatorPanel()
{
  poll_->stop();
  thumbnails_->stop();
  workers_.clear();
  // Workers only read local cached PNGs. Join before the receiver is destroyed;
  // QObject discards its queued image deliveries during base destruction.
  workers_.waitForDone();
}

std::string OperatorPanel::SourceId() const
{
  return sources_->currentData().toString().toUtf8().toStdString();
}

void OperatorPanel::RefreshSources()
{
  const auto choices = backend_->Sources();
  const QString selected = sources_->currentData().toString();
  bool changed = sources_->count() != int(choices.size()) + 1;
  for (int i = 0; !changed && i < int(choices.size()); ++i)
    changed = sources_->itemData(i + 1).toString() != Utf8(choices[i].id) ||
              sources_->itemText(i + 1) != Utf8(choices[i].name);
  if (!changed) return;
  const QSignalBlocker block(sources_);
  sources_->clear();
  sources_->addItem("Choose a PPTBridge Slide source...", QString());
  for (const auto &choice : choices)
    sources_->addItem(Utf8(choice.name), Utf8(choice.id));
  const int index = sources_->findData(selected);
  sources_->setCurrentIndex(index >= 0 ? index : 0);
}

void OperatorPanel::Refresh()
{
  if ((ticks_++ % 4) == 0) RefreshSources();
  const auto id = SourceId();
  auto updated = id.empty() ? OperatorDeck{} : backend_->Read(id);
  const bool reset = selection_.Observe(updated.identity, updated.slides.size(), updated.current);
  deck_ = std::move(updated);
  if (reset) {
    ++image_generation_;
    workers_.clear();
    pending_images_.clear();
    failed_images_.clear();
    visible_icons_.clear();
    images_.clear();
    last_message_.clear();
    rebuilding_ = true;
    const QSignalBlocker block(grid_);
    grid_->clear();
    for (std::size_t i = 0; i < deck_.slides.size(); ++i) {
      auto *item = new QListWidgetItem(grid_);
      item->setToolTip(Utf8(deck_.slides[i].title));
    }
    if (auto selected = selection_.Selected()) grid_->setCurrentRow(int(*selected));
    rebuilding_ = false;
    ApplyFilter();
  }
  for (int i = 0; i < grid_->count(); ++i) {
    const auto &slide = deck_.slides[std::size_t(i)];
    QString prefix = std::size_t(i) == deck_.current ? "NOW | " :
                     std::size_t(i) == deck_.current + 1 ? "NEXT | " : "";
    grid_->item(i)->setText(prefix + QString::number(i + 1) + ". " + Utf8(slide.title));
    QFont font = grid_->font();
    font.setBold(std::size_t(i) == deck_.current);
    grid_->item(i)->setFont(font);
  }
  path_->setText(Utf8(deck_.path));
  path_->setToolTip(Utf8(deck_.path));
  UpdateControls();
  UpdatePreviews();
}

void OperatorPanel::ApplyFilter()
{
  const QString query = search_->text().trimmed();
  for (int i = 0; i < grid_->count(); ++i) {
    const auto &slide = deck_.slides[std::size_t(i)];
    const bool match = query.isEmpty() ||
      Utf8(slide.title).contains(query, Qt::CaseInsensitive) ||
      Utf8(slide.notes).contains(query, Qt::CaseInsensitive);
    grid_->item(i)->setHidden(!match);
  }
  if (auto selected = selection_.Selected()) {
    if (*selected >= std::size_t(grid_->count()) || grid_->item(int(*selected))->isHidden()) {
      const QSignalBlocker block(grid_);
      grid_->setCurrentRow(-1);
      selection_.ClearSelection();
    }
  }
  UpdateControls();
  UpdatePreviews();
}

void OperatorPanel::UpdateControls()
{
  const bool navigate = OperatorCanNavigate(deck_);
  show_->setEnabled(navigate && selection_.Selected().has_value());
  previous_->setEnabled(navigate && deck_.current > 0);
  // Next remains available on the final slide for animation builds. The
  // existing PowerPoint backend prevents advancing past the end of the show.
  next_->setEnabled(navigate);
  back_->setEnabled(navigate && selection_.Previous().has_value());
  black_->setEnabled(deck_.valid && !deck_.loading);
  black_->setText(deck_.black ? "Restore picture" : "Blackout");
  QString text;
  if (SourceId().empty()) text = "Choose the source to control. No deck is selected.";
  else if (!deck_.valid) text = "Source unavailable. No navigation was sent.";
  else {
    text = deck_.in_program ? "TARGET DECK IS IN PROGRAM. " :
      "Target deck is not in Program. ";
    text += Utf8(deck_.capture_status);
    if (deck_.loading) text += " | Loading cached slide assets";
    if (deck_.busy) text += " | PowerPoint command pending";
    if (deck_.live_enabled && !deck_.live_ready)
      text += " | Start/recover Live Mode in the source properties";
    if (!deck_.error.empty()) text += "\n" + Utf8(deck_.error);
  }
  if (!last_message_.isEmpty()) text += "\n" + last_message_;
  status_->setText(text);
}

void OperatorPanel::UpdatePreviews()
{
  auto setImage = [this](QLabel *label, std::optional<std::size_t> index) {
    label->clear();
    if (!index || *index >= deck_.slides.size()) { label->setText("No cached preview"); return; }
    if (auto *image = images_.object(int(*index))) {
      label->setPixmap(image->scaled(std::max(100, label->width()),
        std::max(70, label->height()), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
      label->setText(failed_images_.contains(int(*index)) ? "Cached preview unavailable" : "Loading cached preview...");
    }
  };
  const bool hasSlides = !deck_.slides.empty() && deck_.current < deck_.slides.size();
  current_text_->setText(hasSlides ? QString("Reported current: %1 / %2 (cached)")
    .arg(qulonglong(deck_.current + 1)).arg(qulonglong(deck_.slides.size())) : "Reported current: unavailable");
  const bool hasNext = hasSlides && deck_.current + 1 < deck_.slides.size();
  next_text_->setText(hasNext ? QString("Next: %1 (cached)").arg(qulonglong(deck_.current + 2)) : "Next: end of deck");
  const auto selected = selection_.Selected();
  selected_text_->setText(selected ? QString("PRIVATE SELECTION: %1 - %2")
    .arg(qulonglong(*selected + 1)).arg(Utf8(deck_.slides[*selected].title)) : "PRIVATE SELECTION: none");
  const QString notes = selected ? Utf8(deck_.slides[*selected].notes) : QString();
  if (notes_->toPlainText() != notes) notes_->setPlainText(notes);
  setImage(selected_image_, selected);
  setImage(current_image_, hasSlides ? std::optional<std::size_t>(deck_.current) : std::nullopt);
  setImage(next_image_, hasNext ? std::optional<std::size_t>(deck_.current + 1) : std::nullopt);
}

void OperatorPanel::Submit(OperatorCommand command, std::size_t index)
{
  std::string error;
  // Execute re-reads the UUID, deck generation, availability and busy flag;
  // stale UI selections cannot silently target a replaced deck.
  if (!backend_->Execute(SourceId(), deck_.identity, command, index, error))
    last_message_ = "Not sent: " + Utf8(error);
  else
    last_message_ = "Command submitted. Verify the audience output in OBS Program.";
  Refresh();
}

void OperatorPanel::RequestImage(int index)
{
  if (index < 0 || std::size_t(index) >= deck_.slides.size() || images_.contains(index) ||
      pending_images_.contains(index) || failed_images_.contains(index) || pending_images_.size() >= 8)
    return;
  const QString path = Utf8(deck_.slides[std::size_t(index)].image_path);
  if (path.isEmpty()) { failed_images_.insert(index); return; }
  pending_images_.insert(index);
  const auto generation = image_generation_;
  workers_.start(QRunnable::create([this, index, path, generation] {
    QImageReader reader(path);
    const QSize size = reader.size();
    QImage image;
    if (size.isValid() && size.width() <= 8192 && size.height() <= 8192 &&
        qint64(size.width()) * size.height() <= 33554432) {
      reader.setScaledSize(size.scaled(480, 270, Qt::KeepAspectRatio));
      image = reader.read();
      if (!image.isNull()) image = image.scaled(480, 270, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QMetaObject::invokeMethod(this, [this, index, generation, image] {
      if (generation != image_generation_) return;
      pending_images_.remove(index);
      if (image.isNull()) failed_images_.insert(index);
      else images_.insert(index, new QPixmap(QPixmap::fromImage(image)),
                          std::max(1, int(image.sizeInBytes() / 1024)));
      UpdatePreviews();
    }, Qt::QueuedConnection);
  }));
}

void OperatorPanel::PumpThumbnails()
{
  if (auto selected = selection_.Selected()) RequestImage(int(*selected));
  RequestImage(int(deck_.current));
  RequestImage(int(deck_.current + 1));
  QSet<int> visible;
  for (int i = 0; i < grid_->count(); ++i) {
    auto *item = grid_->item(i);
    if (!item->isHidden() && grid_->visualItemRect(item).intersects(grid_->viewport()->rect())) {
      visible.insert(i);
      RequestImage(i);
      if (auto *image = images_.object(i)) item->setIcon(QIcon(*image));
    }
  }
  // QListWidget icons hold shared pixmaps: release offscreen icons as well as
  // bounding the QCache, otherwise a long deck would bypass the memory bound.
  for (int i : visible_icons_) {
    if (!visible.contains(i) && i < grid_->count()) grid_->item(i)->setIcon(QIcon());
  }
  visible_icons_ = std::move(visible);
}

} // namespace pptbridge
