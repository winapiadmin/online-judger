#include "CompilerDialog.h"
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QVBoxLayout>

CompilerDialog::CompilerDialog(std::vector<GuiCompilerItem> &compilers,
                               QWidget *parent)
    : QDialog(parent), m_compilers(compilers) {
  setWindowTitle(tr("Configure Compilers"));
  resize(700, 400);

  auto *layout = new QVBoxLayout(this);

  m_table = new QTableWidget(static_cast<int>(compilers.size()), 2);
  m_table->setHorizontalHeaderLabels({tr("Extension"), tr("Command")});
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->verticalHeader()->setVisible(false);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

  for (int i = 0; i < static_cast<int>(compilers.size()); ++i) {
    m_table->setItem(i, 0, new QTableWidgetItem(compilers[i].ext));
    m_table->setItem(i, 1, new QTableWidgetItem(compilers[i].cmd));
  }

  layout->addWidget(m_table);

  auto *btnLayout = new QHBoxLayout();
  auto *insertBtn = new QPushButton(tr("Insert"));
  auto *removeBtn = new QPushButton(tr("Remove"));
  auto *upBtn = new QPushButton(tr("Move Up"));
  auto *downBtn = new QPushButton(tr("Move Down"));
  btnLayout->addWidget(insertBtn);
  btnLayout->addWidget(removeBtn);
  btnLayout->addWidget(upBtn);
  btnLayout->addWidget(downBtn);
  btnLayout->addStretch();
  layout->addLayout(btnLayout);

  connect(insertBtn, &QPushButton::clicked, this, &CompilerDialog::onInsert);
  connect(removeBtn, &QPushButton::clicked, this, &CompilerDialog::onRemove);
  connect(upBtn, &QPushButton::clicked, this, &CompilerDialog::onMoveUp);
  connect(downBtn, &QPushButton::clicked, this, &CompilerDialog::onMoveDown);

  auto *shortcutIns = new QShortcut(QKeySequence(Qt::Key_Insert), this);
  connect(shortcutIns, &QShortcut::activated, this, &CompilerDialog::onInsert);

  auto *shortcutDel = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Delete), this);
  connect(shortcutDel, &QShortcut::activated, this, &CompilerDialog::onRemove);

  auto *shortcutUp = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_U), this);
  connect(shortcutUp, &QShortcut::activated, this, &CompilerDialog::onMoveUp);

  auto *shortcutDown = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this);
  connect(shortcutDown, &QShortcut::activated, this,
          &CompilerDialog::onMoveDown);

  auto *btnBox = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(btnBox, &QDialogButtonBox::accepted, this, [this]() {
    m_compilers.clear();
    for (int i = 0; i < m_table->rowCount(); ++i) {
      m_compilers.push_back({m_table->item(i, 0)->text(),
                             m_table->item(i, 1)->text()});
    }
    accept();
  });
  connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(btnBox);
}

void CompilerDialog::onInsert() {
  int row = m_table->currentRow();
  if (row < 0)
    row = m_table->rowCount();
  else
    ++row;
  m_table->insertRow(row);
  m_table->setItem(row, 0, new QTableWidgetItem(".ext"));
  m_table->setItem(row, 1, new QTableWidgetItem("compiler command"));
  m_table->setCurrentCell(row, 0);
}

void CompilerDialog::onRemove() {
  int row = m_table->currentRow();
  if (row >= 0)
    m_table->removeRow(row);
}

void CompilerDialog::onMoveUp() {
  int row = m_table->currentRow();
  if (row <= 0)
    return;
  for (int c = 0; c < m_table->columnCount(); ++c) {
    auto *a = m_table->takeItem(row, c);
    auto *b = m_table->takeItem(row - 1, c);
    m_table->setItem(row, c, b);
    m_table->setItem(row - 1, c, a);
  }
  m_table->setCurrentCell(row - 1, m_table->currentColumn());
}

void CompilerDialog::onMoveDown() {
  int row = m_table->currentRow();
  if (row < 0 || row >= m_table->rowCount() - 1)
    return;
  for (int c = 0; c < m_table->columnCount(); ++c) {
    auto *a = m_table->takeItem(row, c);
    auto *b = m_table->takeItem(row + 1, c);
    m_table->setItem(row, c, b);
    m_table->setItem(row + 1, c, a);
  }
  m_table->setCurrentCell(row + 1, m_table->currentColumn());
}
