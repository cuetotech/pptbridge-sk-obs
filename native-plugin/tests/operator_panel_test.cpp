#include "operator_panel.hpp"
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace pptbridge;
int checks = 0;
void require(bool value, const char *message)
{ ++checks; std::cerr << "Check " << checks << ": " << message << std::endl; if (!value) throw std::runtime_error(message); }
struct Fake final : OperatorBackend {
  OperatorDeck deck;
  int commands = 0;
  OperatorCommand last = OperatorCommand::Next;
  std::size_t target = 0;
  bool missing = false;
  std::vector<OperatorSource> Sources() override
  { return missing ? std::vector<OperatorSource>{} : std::vector<OperatorSource>{{"source-a", "Test slides"}}; }
  OperatorDeck Read(const std::string &) override { return missing ? OperatorDeck{} : deck; }
  bool Execute(const std::string &, const std::string &identity, OperatorCommand command,
               std::size_t index, std::string &error) override
  {
    if (identity != deck.identity || missing) { error = "Stale deck"; return false; }
    if (command != OperatorCommand::Black && !OperatorCanNavigate(deck)) { error = "Not ready"; return false; }
    ++commands; last = command; target = index;
    if (command == OperatorCommand::Show) deck.current = index;
    if (command == OperatorCommand::Next && deck.current + 1 < deck.slides.size()) ++deck.current;
    if (command == OperatorCommand::Previous && deck.current > 0) --deck.current;
    if (command == OperatorCommand::Black) deck.black = !deck.black;
    return true;
  }
};
template <class T> T *find(OperatorPanel &panel, const char *name)
{ auto *result = panel.findChild<T *>(name); if (!result) throw std::runtime_error(name); return result; }
int main(int argc, char **argv)
{
  std::cerr << "Starting Qt panel test" << std::endl;
  QApplication app(argc, argv);
  std::cerr << "Qt application initialized" << std::endl;
  try {
    QTemporaryDir assets;
    QImage image(640, 360, QImage::Format_RGB32);
    image.fill(Qt::darkBlue);
    {
      QPainter painter(&image);
      painter.setPen(Qt::white);
      QFont font = painter.font(); font.setPointSize(24); painter.setFont(font);
      painter.drawText(image.rect(), Qt::AlignCenter, "Slide preview\nTest fixture - not PowerPoint");
    }
    const auto path = assets.path() + "/slide.png";
    require(image.save(path), "thumbnail fixture");
    auto fake = std::make_unique<Fake>();
    auto *backend = fake.get();
    backend->deck.identity = "a:1";
    backend->deck.name = "Test deck";
    backend->deck.path = "C:/operator-test.pptx";
    backend->deck.valid = true;
    backend->deck.capture_status = "Cached test fixture (not live capture)";
    backend->deck.slides = {{"Verse one", "Opening notes", path.toUtf8().toStdString()},
      {u8"Verse two \uC740\uD61C", "Repeat here", path.toUtf8().toStdString()},
      {"Ending", "", path.toUtf8().toStdString()}};
    OperatorPanel panel(std::move(fake));
    panel.resize(760, 1000); panel.show(); app.processEvents();
    auto *sources = find<QComboBox>(panel, "deckSelector");
    auto *grid = find<QListWidget>(panel, "slideGrid");
    auto *show = find<QPushButton>(panel, "showSelected");
    auto *next = find<QPushButton>(panel, "nextSlide");
    auto *back = find<QPushButton>(panel, "lastShown");
    auto *black = find<QPushButton>(panel, "blackout");
    auto *search = find<QLineEdit>(panel, "searchSlides");
    require(!show->isEnabled() && sources->currentIndex() == 0, "explicit source choice required");
    sources->setCurrentIndex(1); panel.Refresh();
    require(grid->count() == 3 && show->isEnabled(), "deck loaded into panel");
    search->setText(QString::fromUtf8(u8"\uC740\uD61C"));
    require(grid->item(0)->isHidden() && !grid->item(1)->isHidden(), "Korean title search");
    search->clear();
    grid->setCurrentRow(1);
    require(backend->commands == 0 && backend->deck.current == 0, "thumbnail browsing has no command");
    panel.Refresh();
    require(grid->currentRow() == 1, "poll does not steal selection");
    show->click();
    require(backend->commands == 1 && backend->target == 1 && backend->deck.current == 1, "show selected sends one exact jump");
    require(back->isEnabled(), "observed navigation enables back");
    back->click();
    require(backend->target == 0 && backend->deck.current == 0, "back uses observed history");
    backend->deck.current = 2; panel.Refresh(); back->click();
    require(backend->target == 0, "external navigation updates history");
    grid->setCurrentRow(1); search->setText("Ending");
    require(!show->isEnabled() && grid->item(1)->isHidden(), "filter clears hidden selection");
    search->clear(); grid->setCurrentRow(2);
    const int before = backend->commands;
    backend->deck.identity = "a:2";
    show->click();
    require(backend->commands == before, "stale deck identity rejects command");
    backend->deck.busy = true; panel.Refresh();
    require(!show->isEnabled() && !next->isEnabled(), "pending command disables navigation");
    require(black->isEnabled(), "blackout remains available during a command");
    black->click(); require(backend->deck.black, "blackout command delivered");
    backend->deck.busy = false; backend->deck.live_enabled = true; backend->deck.live_ready = false; panel.Refresh();
    require(!show->isEnabled(), "disconnected live mode is not a cached navigation fallback");
    backend->deck.live_ready = true; panel.Refresh();
    require(next->isEnabled(), "ready live mode enables navigation");
    // Let the real background thumbnail pipeline run before destruction.
    for (int i = 0; i < 15; ++i) { app.processEvents(); QThread::msleep(25); }
    require(grid->visualItemRect(grid->item(0)).height() >= 130,
            "thumbnail cell preserves readable image and caption height");
    require(!grid->item(0)->icon().isNull(), "visible thumbnail decoded and installed");
    require(grid->item(0)->text().contains("Verse one"), "thumbnail caption retained");
    require(panel.grab().save("operator-panel-smoke.png"), "Qt panel screenshot saved");
    backend->missing = true;
    for (int i = 0; i < 5; ++i) panel.Refresh();
    require(sources->currentIndex() == 0 && !show->isEnabled(), "source removal fails closed");
    std::cout << checks << " Qt interaction checks passed\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
