#pragma once
#include "ContestData.h"
#include <QDialog>
#include <vector>

class QTableWidget;

class CompilerDialog : public QDialog {
  Q_OBJECT
public:
  explicit CompilerDialog(std::vector<GuiCompilerItem> &compilers,
                          QWidget *parent = nullptr);

private slots:
  void onInsert();
  void onRemove();
  void onMoveUp();
  void onMoveDown();

private:
  std::vector<GuiCompilerItem> &m_compilers;
  QTableWidget *m_table;
};
