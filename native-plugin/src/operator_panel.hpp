#pragma once
#include "operator_model.hpp"
#include <QCache>
#include <QPixmap>
#include <QSet>
#include <QThreadPool>
#include <QWidget>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QPlainTextEdit;
class QTimer;

namespace pptbridge {

// Backend injection permits real Qt interaction tests without OBS/PowerPoint.
class OperatorPanel final : public QWidget {
public:
  explicit OperatorPanel(std::unique_ptr<OperatorBackend> backend, QWidget *parent = nullptr);
  ~OperatorPanel() override;
  void Refresh();
private:
  void RefreshSources();
  void ApplyFilter();
  void UpdateControls();
  void UpdatePreviews();
  void PumpThumbnails();
  void RequestImage(int index);
  void Submit(OperatorCommand command, std::size_t index = 0);
  std::string SourceId() const;
  std::unique_ptr<OperatorBackend> backend_;
  OperatorDeck deck_;
  OperatorSelection selection_;
  QComboBox *sources_ = nullptr;
  QLineEdit *search_ = nullptr;
  QListWidget *grid_ = nullptr;
  QLabel *status_ = nullptr, *path_ = nullptr;
  QLabel *selected_image_ = nullptr, *current_image_ = nullptr, *next_image_ = nullptr;
  QLabel *selected_text_ = nullptr, *current_text_ = nullptr, *next_text_ = nullptr;
  QPlainTextEdit *notes_ = nullptr;
  QPushButton *show_ = nullptr, *previous_ = nullptr, *next_ = nullptr;
  QPushButton *back_ = nullptr, *black_ = nullptr;
  QTimer *poll_ = nullptr, *thumbnails_ = nullptr;
  QThreadPool workers_;
  QCache<int, QPixmap> images_{24 * 1024}; // costs in KiB, not image count
  QSet<int> pending_images_, failed_images_, visible_icons_;
  quint64 image_generation_ = 0;
  unsigned ticks_ = 0;
  bool rebuilding_ = false;
  QString last_message_;
};

} // namespace pptbridge
