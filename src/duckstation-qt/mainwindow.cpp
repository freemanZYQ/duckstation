#include "mainwindow.h"
#include "core/game_list.h"
#include "core/settings.h"
#include "gamelistsettingswidget.h"
#include "gamelistwidget.h"
#include "qthostinterface.h"
#include "qtsettingsinterface.h"
#include "settingsdialog.h"
#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <cmath>

static constexpr char DISC_IMAGE_FILTER[] =
  "All File Types (*.bin *.img *.cue *.exe *.psexe);;Single-Track Raw Images (*.bin *.img);;Cue Sheets "
  "(*.cue);;MAME CHD Images (*.chd);;PlayStation Executables (*.exe *.psexe)";

MainWindow::MainWindow(QtHostInterface* host_interface) : QMainWindow(nullptr), m_host_interface(host_interface)
{
  m_ui.setupUi(this);
  setupAdditionalUi();
  connectSignals();
  populateLoadSaveStateMenus(QString());

  resize(750, 690);
}

MainWindow::~MainWindow()
{
  delete m_display_widget;
  m_host_interface->displayWidgetDestroyed();
}

void MainWindow::reportError(QString message)
{
  QMessageBox::critical(nullptr, tr("DuckStation Error"), message, QMessageBox::Ok);
}

void MainWindow::reportMessage(QString message)
{
  m_ui.statusBar->showMessage(message, 2000);
}

void MainWindow::onEmulationStarting()
{
  switchToEmulationView();
  updateEmulationActions(true, false);

  // we need the surface visible..
  QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void MainWindow::onEmulationStarted()
{
  m_emulation_running = true;
  updateEmulationActions(false, true);
}

void MainWindow::onEmulationStopped()
{
  m_emulation_running = false;
  updateEmulationActions(false, false);
  switchToGameListView();
}

void MainWindow::onEmulationPaused(bool paused)
{
  m_ui.actionPause->setChecked(paused);
}

void MainWindow::toggleFullscreen()
{
  const bool fullscreen = !m_display_widget->isFullScreen();
  if (fullscreen)
  {
    m_ui.mainContainer->setCurrentIndex(0);
    m_ui.mainContainer->removeWidget(m_display_widget);
    m_display_widget->setParent(nullptr);
    m_display_widget->showFullScreen();
  }
  else
  {
    m_ui.mainContainer->insertWidget(1, m_display_widget);
    m_ui.mainContainer->setCurrentIndex(1);
  }

  m_display_widget->setFocus();

  QSignalBlocker blocker(m_ui.actionFullscreen);
  m_ui.actionFullscreen->setChecked(fullscreen);
}

void MainWindow::recreateDisplayWidget(bool create_device_context)
{
  const bool was_fullscreen = m_display_widget->isFullScreen();
  if (was_fullscreen)
    toggleFullscreen();

  switchToGameListView();

  // recreate the display widget using the potentially-new renderer
  m_ui.mainContainer->removeWidget(m_display_widget);
  m_host_interface->displayWidgetDestroyed();
  delete m_display_widget;
  m_display_widget = m_host_interface->createDisplayWidget(m_ui.mainContainer);
  m_ui.mainContainer->insertWidget(1, m_display_widget);

  if (create_device_context)
    switchToEmulationView();

  // we need the surface visible..
  QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

  if (create_device_context && !m_host_interface->createDisplayDeviceContext())
  {
    QMessageBox::critical(this, tr("DuckStation Error"),
                          tr("Failed to create new device context on renderer switch. Cannot continue."));
    QCoreApplication::exit();
    return;
  }

  updateDebugMenuGPURenderer();
}

void MainWindow::onPerformanceCountersUpdated(float speed, float fps, float vps, float average_frame_time,
                                              float worst_frame_time)
{
  m_status_speed_widget->setText(QStringLiteral("%1%").arg(speed, 0, 'f', 0));
  m_status_fps_widget->setText(
    QStringLiteral("FPS: %1/%2").arg(std::round(fps), 0, 'f', 0).arg(std::round(vps), 0, 'f', 0));
  m_status_frame_time_widget->setText(
    QStringLiteral("%1ms average, %2ms worst").arg(average_frame_time, 0, 'f', 2).arg(worst_frame_time, 0, 'f', 2));
}

void MainWindow::onRunningGameChanged(QString filename, QString game_code, QString game_title)
{
  populateLoadSaveStateMenus(game_code);
  if (game_title.isEmpty())
    setWindowTitle(tr("DuckStation"));
  else
    setWindowTitle(game_title);
}

void MainWindow::onStartDiscActionTriggered()
{
  QString filename =
    QFileDialog::getOpenFileName(this, tr("Select Disc Image"), QString(), tr(DISC_IMAGE_FILTER), nullptr);
  if (filename.isEmpty())
    return;

  m_host_interface->bootSystem(std::move(filename), QString());
}

void MainWindow::onChangeDiscFromFileActionTriggered()
{
  QString filename =
    QFileDialog::getOpenFileName(this, tr("Select Disc Image"), QString(), tr(DISC_IMAGE_FILTER), nullptr);
  if (filename.isEmpty())
    return;

  m_host_interface->changeDisc(filename);
}

void MainWindow::onChangeDiscFromGameListActionTriggered()
{
  m_host_interface->pauseSystem(true);
  switchToGameListView();
}

void MainWindow::onStartBiosActionTriggered()
{
  m_host_interface->bootSystem(QString(), QString());
}

static void OpenURL(QWidget* parent, const char* url)
{
  const QUrl qurl(QUrl::fromEncoded(QByteArray(url, std::strlen(url))));
  if (!QDesktopServices::openUrl(qurl))
  {
    QMessageBox::critical(parent, QObject::tr("Failed to open URL"),
                          QObject::tr("Failed to open URL.\n\nThe URL was: %1").arg(qurl.toString()));
  }
}

void MainWindow::onGitHubRepositoryActionTriggered()
{
  OpenURL(this, "https://github.com/stenzek/duckstation/");
}

void MainWindow::onIssueTrackerActionTriggered()
{
  OpenURL(this, "https://github.com/stenzek/duckstation/issues");
}

void MainWindow::onAboutActionTriggered() {}

void MainWindow::setupAdditionalUi()
{
  m_game_list_widget = new GameListWidget(m_ui.mainContainer);
  m_game_list_widget->initialize(m_host_interface);
  m_ui.mainContainer->insertWidget(0, m_game_list_widget);

  m_display_widget = m_host_interface->createDisplayWidget(m_ui.mainContainer);
  m_ui.mainContainer->insertWidget(1, m_display_widget);

  m_ui.mainContainer->setCurrentIndex(0);

  m_status_speed_widget = new QLabel(m_ui.statusBar);
  m_status_speed_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_status_speed_widget->setFixedSize(40, 16);
  m_status_speed_widget->hide();

  m_status_fps_widget = new QLabel(m_ui.statusBar);
  m_status_fps_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_status_fps_widget->setFixedSize(80, 16);
  m_status_fps_widget->hide();

  m_status_frame_time_widget = new QLabel(m_ui.statusBar);
  m_status_frame_time_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_status_frame_time_widget->setFixedSize(190, 16);
  m_status_frame_time_widget->hide();

  for (u32 i = 0; i < static_cast<u32>(GPURenderer::Count); i++)
  {
    const GPURenderer renderer = static_cast<GPURenderer>(i);
    QAction* action = m_ui.menuRenderer->addAction(tr(Settings::GetRendererDisplayName(renderer)));
    action->setCheckable(true);
    connect(action, &QAction::triggered, [this, action, renderer]() {
      m_host_interface->putSettingValue(QStringLiteral("GPU/Renderer"), QString(Settings::GetRendererName(renderer)));
      m_host_interface->applySettings();
    });
  }
  updateDebugMenuGPURenderer();
}

void MainWindow::updateEmulationActions(bool starting, bool running)
{
  m_ui.actionStartDisc->setDisabled(starting || running);
  m_ui.actionStartBios->setDisabled(starting || running);
  m_ui.actionPowerOff->setDisabled(starting || running);

  m_ui.actionPowerOff->setDisabled(starting || !running);
  m_ui.actionReset->setDisabled(starting || !running);
  m_ui.actionPause->setDisabled(starting || !running);
  m_ui.actionChangeDisc->setDisabled(starting || !running);
  m_ui.menuChangeDisc->setDisabled(starting || !running);

  m_ui.actionSaveState->setDisabled(starting || !running);
  m_ui.menuSaveState->setDisabled(starting || !running);

  m_ui.actionFullscreen->setDisabled(starting || !running);

  if (running && m_status_speed_widget->isHidden())
  {
    m_status_speed_widget->show();
    m_status_fps_widget->show();
    m_status_frame_time_widget->show();
    m_ui.statusBar->addPermanentWidget(m_status_speed_widget);
    m_ui.statusBar->addPermanentWidget(m_status_fps_widget);
    m_ui.statusBar->addPermanentWidget(m_status_frame_time_widget);
  }
  else if (!running && m_status_speed_widget->isVisible())
  {
    m_ui.statusBar->removeWidget(m_status_speed_widget);
    m_ui.statusBar->removeWidget(m_status_fps_widget);
    m_ui.statusBar->removeWidget(m_status_frame_time_widget);
    m_status_speed_widget->hide();
    m_status_fps_widget->hide();
    m_status_frame_time_widget->hide();
  }

  m_ui.statusBar->clearMessage();
}

void MainWindow::switchToGameListView()
{
  m_ui.mainContainer->setCurrentIndex(0);
}

void MainWindow::switchToEmulationView()
{
  m_ui.mainContainer->setCurrentIndex(1);
  m_display_widget->setFocus();
}

void MainWindow::connectSignals()
{
  updateEmulationActions(false, false);
  onEmulationPaused(false);

  connect(m_ui.actionStartDisc, &QAction::triggered, this, &MainWindow::onStartDiscActionTriggered);
  connect(m_ui.actionStartBios, &QAction::triggered, this, &MainWindow::onStartBiosActionTriggered);
  connect(m_ui.actionChangeDisc, &QAction::triggered, [this] { m_ui.menuChangeDisc->exec(QCursor::pos()); });
  connect(m_ui.actionChangeDiscFromFile, &QAction::triggered, this, &MainWindow::onChangeDiscFromFileActionTriggered);
  connect(m_ui.actionChangeDiscFromGameList, &QAction::triggered, this,
          &MainWindow::onChangeDiscFromGameListActionTriggered);
  connect(m_ui.actionAddGameDirectory, &QAction::triggered,
          [this]() { getSettingsDialog()->getGameListSettingsWidget()->addSearchDirectory(this); });
  connect(m_ui.actionPowerOff, &QAction::triggered, [this]() { m_host_interface->powerOffSystem(true, false); });
  connect(m_ui.actionReset, &QAction::triggered, m_host_interface, &QtHostInterface::resetSystem);
  connect(m_ui.actionPause, &QAction::toggled, m_host_interface, &QtHostInterface::pauseSystem);
  connect(m_ui.actionLoadState, &QAction::triggered, this, [this]() { m_ui.menuLoadState->exec(QCursor::pos()); });
  connect(m_ui.actionSaveState, &QAction::triggered, this, [this]() { m_ui.menuSaveState->exec(QCursor::pos()); });
  connect(m_ui.actionExit, &QAction::triggered, this, &MainWindow::close);
  connect(m_ui.actionFullscreen, &QAction::triggered, this, &MainWindow::toggleFullscreen);
  connect(m_ui.actionSettings, &QAction::triggered, [this]() { doSettings(SettingsDialog::Category::Count); });
  connect(m_ui.actionConsoleSettings, &QAction::triggered,
          [this]() { doSettings(SettingsDialog::Category::ConsoleSettings); });
  connect(m_ui.actionGameListSettings, &QAction::triggered,
          [this]() { doSettings(SettingsDialog::Category::GameListSettings); });
  connect(m_ui.actionHotkeySettings, &QAction::triggered,
          [this]() { doSettings(SettingsDialog::Category::HotkeySettings); });
  connect(m_ui.actionPortSettings, &QAction::triggered,
          [this]() { doSettings(SettingsDialog::Category::PortSettings); });
  connect(m_ui.actionGPUSettings, &QAction::triggered, [this]() { doSettings(SettingsDialog::Category::GPUSettings); });
  connect(m_ui.actionAudioSettings, &QAction::triggered,
          [this]() { doSettings(SettingsDialog::Category::AudioSettings); });
  connect(m_ui.actionGitHubRepository, &QAction::triggered, this, &MainWindow::onGitHubRepositoryActionTriggered);
  connect(m_ui.actionIssueTracker, &QAction::triggered, this, &MainWindow::onIssueTrackerActionTriggered);
  connect(m_ui.actionAbout, &QAction::triggered, this, &MainWindow::onAboutActionTriggered);

  connect(m_host_interface, &QtHostInterface::errorReported, this, &MainWindow::reportError,
          Qt::BlockingQueuedConnection);
  connect(m_host_interface, &QtHostInterface::messageReported, this, &MainWindow::reportMessage);
  connect(m_host_interface, &QtHostInterface::emulationStarting, this, &MainWindow::onEmulationStarting);
  connect(m_host_interface, &QtHostInterface::emulationStarted, this, &MainWindow::onEmulationStarted);
  connect(m_host_interface, &QtHostInterface::emulationStopped, this, &MainWindow::onEmulationStopped);
  connect(m_host_interface, &QtHostInterface::emulationPaused, this, &MainWindow::onEmulationPaused);
  connect(m_host_interface, &QtHostInterface::toggleFullscreenRequested, this, &MainWindow::toggleFullscreen);
  connect(m_host_interface, &QtHostInterface::recreateDisplayWidgetRequested, this, &MainWindow::recreateDisplayWidget,
          Qt::BlockingQueuedConnection);
  connect(m_host_interface, &QtHostInterface::performanceCountersUpdated, this,
          &MainWindow::onPerformanceCountersUpdated);
  connect(m_host_interface, &QtHostInterface::runningGameChanged, this, &MainWindow::onRunningGameChanged);

  connect(m_game_list_widget, &GameListWidget::bootEntryRequested, [this](const GameListEntry* entry) {
    // if we're not running, boot the system, otherwise swap discs
    QString path = QString::fromStdString(entry->path);
    if (!m_emulation_running)
    {
      m_host_interface->bootSystem(path, QString());
    }
    else
    {
      m_host_interface->changeDisc(path);
      m_host_interface->pauseSystem(false);
      switchToEmulationView();
    }
  });
  connect(m_game_list_widget, &GameListWidget::entrySelected, [this](const GameListEntry* entry) {
    if (!entry)
    {
      m_ui.statusBar->clearMessage();
      populateLoadSaveStateMenus(QString());
      return;
    }

    m_ui.statusBar->showMessage(QString::fromStdString(entry->path));
    populateLoadSaveStateMenus(QString::fromStdString(entry->code));
  });
}

SettingsDialog* MainWindow::getSettingsDialog()
{
  if (!m_settings_dialog)
    m_settings_dialog = new SettingsDialog(m_host_interface, this);

  return m_settings_dialog;
}

void MainWindow::doSettings(SettingsDialog::Category category)
{
  SettingsDialog* dlg = getSettingsDialog();
  if (!dlg->isVisible())
  {
    dlg->setModal(false);
    dlg->show();
  }

  if (category != SettingsDialog::Category::Count)
    dlg->setCategory(category);
}

void MainWindow::updateDebugMenuGPURenderer()
{
  // update the menu with the new selected renderer
  std::optional<GPURenderer> current_renderer = Settings::ParseRendererName(
    m_host_interface->getSettingValue(QStringLiteral("GPU/Renderer")).toString().toStdString().c_str());
  if (current_renderer.has_value())
  {
    const QString current_renderer_display_name(
      QString::fromUtf8(Settings::GetRendererDisplayName(current_renderer.value())));
    for (QObject* obj : m_ui.menuRenderer->children())
    {
      QAction* action = qobject_cast<QAction*>(obj);
      if (action)
        action->setChecked(action->text() == current_renderer_display_name);
    }
  }
}

void MainWindow::populateLoadSaveStateMenus(QString game_code)
{
  static constexpr int NUM_SAVE_STATE_SLOTS = 10;

  QMenu* const load_menu = m_ui.menuLoadState;
  QMenu* const save_menu = m_ui.menuSaveState;

  load_menu->clear();
  save_menu->clear();

  load_menu->addAction(tr("Resume State"));
  load_menu->addSeparator();

  for (int i = 0; i < NUM_SAVE_STATE_SLOTS; i++)
  {
    // TODO: Do we want to test for the existance of these on disk here?
    load_menu->addAction(tr("Global Save %1 (2020-01-01 00:01:02)").arg(i + 1));
    save_menu->addAction(tr("Global Save %1").arg(i + 1));
  }

  if (!game_code.isEmpty())
  {
    load_menu->addSeparator();
    save_menu->addSeparator();

    for (int i = 0; i < NUM_SAVE_STATE_SLOTS; i++)
    {
      load_menu->addAction(tr("Game Save %1 (2020-01-01 00:01:02)").arg(i + 1));
      save_menu->addAction(tr("Game Save %1").arg(i + 1));
    }
  }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
  m_host_interface->powerOffSystem(true, true);
  QMainWindow::closeEvent(event);
}
