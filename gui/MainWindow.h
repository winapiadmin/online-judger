#pragma once
#include "ContestData.h"
#include "ContestModel.h"
#include "common/AppConfig.h"
#include <QMainWindow>
#include <atomic>

class QProgressBar;
class QPushButton;
class QTableView;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

private slots:
  void onSelectTests();
  void onSelectSubmissions();
  void onLoadContest();
  void onSaveContest();
  void onExportCSV();
  void onConfigureCompilers();
  void onTableContextMenu(const QPoint &pos);
  void onReTest(int problemCol);
  void onCancelTest();
  void onEraseScores(int problemCol);
  void onModifyTestData(int problemCol);
  void onDetailedResult(int problemCol);
  void onAddContestant();
  void onRemoveContestant();
  void onAddProblem();

private:
  void startBatchReTest(const QString &problem);
  void syncConfigToData();
  void syncDataToConfig();

  AppConfig m_config;
  GuiContestData m_data;
  ContestModel *m_model;
  QTableView *m_table;

  QProgressBar *m_progressBar;
  QPushButton *m_cancelBtn;

  QString m_contestFilePath;
  std::atomic<bool> m_cancelRequested{false};
  std::atomic<bool> m_testRunning{false};
};
