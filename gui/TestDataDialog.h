#pragma once
#include "ContestData.h"
#include <QDialog>

class QLineEdit;
class QCheckBox;
class QTableWidget;
class QPushButton;

class TestDataDialog : public QDialog {
  Q_OBJECT
public:
  explicit TestDataDialog(const std::filesystem::path &testsDir,
                          const QString &problemName, QWidget *parent = nullptr);

private slots:
  void onAddSubtest();
  void onRemoveSubtest();
  void onSave();
  void onBrowseInput();
  void onBrowseOutput();

private:
  void loadFromConfig();

  std::filesystem::path m_testsDir;
  QString m_problemName;
  GuiProblemConfig m_config;

  QLineEdit *m_nameEdit;
  QLineEdit *m_inputEdit;
  QLineEdit *m_outputEdit;
  QLineEdit *m_evaluatorEdit;
  QCheckBox *m_stdInCheck;
  QCheckBox *m_stdOutCheck;
  QLineEdit *m_memoryEdit;
  QLineEdit *m_timeEdit;
  QLineEdit *m_markEdit;
  QTableWidget *m_subtestTable;
  QPushButton *m_saveBtn;
};
