#include "TestDataDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

TestDataDialog::TestDataDialog(const std::filesystem::path &testsDir,
                               const QString &problemName, QWidget *parent)
    : QDialog(parent), m_testsDir(testsDir), m_problemName(problemName) {
  setWindowTitle(tr("Modify Test Data - %1").arg(problemName));
  resize(600, 500);

  auto *mainLayout = new QVBoxLayout(this);

  auto *formLayout = new QFormLayout();
  m_nameEdit = new QLineEdit();
  m_nameEdit->setReadOnly(true);
  formLayout->addRow(tr("Name:"), m_nameEdit);

  auto *ioWidget = new QWidget();
  auto *ioLayout = new QHBoxLayout(ioWidget);
  ioLayout->setContentsMargins(0, 0, 0, 0);
  m_inputEdit = new QLineEdit();
  m_inputEdit->setPlaceholderText(tr("e.g. PROBLEM.INP"));
  auto *browseInBtn = new QPushButton(tr("Browse..."));
  connect(browseInBtn, &QPushButton::clicked, this,
          &TestDataDialog::onBrowseInput);
  ioLayout->addWidget(m_inputEdit);
  ioLayout->addWidget(browseInBtn);
  formLayout->addRow(tr("Input File:"), ioWidget);

  auto *outWidget = new QWidget();
  auto *outLayout = new QHBoxLayout(outWidget);
  outLayout->setContentsMargins(0, 0, 0, 0);
  m_outputEdit = new QLineEdit();
  m_outputEdit->setPlaceholderText(tr("e.g. PROBLEM.OUT"));
  auto *browseOutBtn = new QPushButton(tr("Browse..."));
  connect(browseOutBtn, &QPushButton::clicked, this,
          &TestDataDialog::onBrowseOutput);
  outLayout->addWidget(m_outputEdit);
  outLayout->addWidget(browseOutBtn);
  formLayout->addRow(tr("Output File:"), outWidget);

  m_evaluatorEdit = new QLineEdit();
  m_evaluatorEdit->setPlaceholderText(tr("e.g. C1LinesWordsIgnoreCase"));
  formLayout->addRow(tr("Evaluator:"), m_evaluatorEdit);

  m_stdInCheck = new QCheckBox(tr("Use stdin for input"));
  formLayout->addRow(m_stdInCheck);

  m_stdOutCheck = new QCheckBox(tr("Use stdout for output"));
  formLayout->addRow(m_stdOutCheck);

  m_memoryEdit = new QLineEdit();
  m_memoryEdit->setPlaceholderText(tr("MiB"));
  formLayout->addRow(tr("Memory Limit:"), m_memoryEdit);

  m_timeEdit = new QLineEdit();
  m_timeEdit->setPlaceholderText(tr("seconds"));
  formLayout->addRow(tr("Time Limit:"), m_timeEdit);

  m_markEdit = new QLineEdit();
  m_markEdit->setPlaceholderText(tr("score weight"));
  formLayout->addRow(tr("Mark:"), m_markEdit);

  mainLayout->addLayout(formLayout);

  auto *subtestLabel = new QLabel(tr("Subtests:"));
  mainLayout->addWidget(subtestLabel);

  m_subtestTable = new QTableWidget(0, 4);
  m_subtestTable->setHorizontalHeaderLabels(
      {tr("Name"), tr("Mark"), tr("Time"), tr("Memory")});
  m_subtestTable->horizontalHeader()->setStretchLastSection(true);
  m_subtestTable->verticalHeader()->setVisible(false);
  mainLayout->addWidget(m_subtestTable);

  auto *subBtnLayout = new QHBoxLayout();
  auto *addBtn = new QPushButton(tr("Add Subtest"));
  auto *removeBtn = new QPushButton(tr("Remove Subtest"));
  connect(addBtn, &QPushButton::clicked, this, &TestDataDialog::onAddSubtest);
  connect(removeBtn, &QPushButton::clicked, this,
          &TestDataDialog::onRemoveSubtest);
  subBtnLayout->addWidget(addBtn);
  subBtnLayout->addWidget(removeBtn);
  subBtnLayout->addStretch();
  mainLayout->addLayout(subBtnLayout);

  auto *btnBox = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  connect(btnBox, &QDialogButtonBox::accepted, this, &TestDataDialog::onSave);
  connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(btnBox);

  loadFromConfig();
}

void TestDataDialog::loadFromConfig() {
  readProblemConfig(m_testsDir, m_problemName.toStdString(), m_config);

  m_nameEdit->setText(m_config.name);
  m_inputEdit->setText(m_config.inputFile);
  m_outputEdit->setText(m_config.outputFile);
  m_evaluatorEdit->setText(m_config.evaluatorName);
  m_stdInCheck->setChecked(m_config.useStdIn);
  m_stdOutCheck->setChecked(m_config.useStdOut);
  m_memoryEdit->setText(QString::number(m_config.memoryLimit));
  m_timeEdit->setText(QString::number(m_config.timeLimit, 'f', 2));
  m_markEdit->setText(QString::number(m_config.mark, 'f', 2));

  m_subtestTable->setRowCount(static_cast<int>(m_config.subtests.size()));
  for (int i = 0; i < static_cast<int>(m_config.subtests.size()); ++i) {
    auto &st = m_config.subtests[i];
    m_subtestTable->setItem(i, 0, new QTableWidgetItem(st.name));
    auto *markItem = new QTableWidgetItem(QString::number(st.mark, 'f', 2));
    m_subtestTable->setItem(i, 1, markItem);
    auto *tlItem =
        new QTableWidgetItem(QString::number(st.timeLimit, 'f', 2));
    m_subtestTable->setItem(i, 2, tlItem);
    auto *mlItem = new QTableWidgetItem(QString::number(st.memoryLimit));
    m_subtestTable->setItem(i, 3, mlItem);
  }
}

void TestDataDialog::onAddSubtest() {
  int row = m_subtestTable->rowCount();
  m_subtestTable->insertRow(row);
  m_subtestTable->setItem(
      row, 0,
      new QTableWidgetItem(tr("Test%1").arg(row + 1, 2, 10, QChar('0'))));
  m_subtestTable->setItem(row, 1, new QTableWidgetItem("1.00"));
  m_subtestTable->setItem(row, 2, new QTableWidgetItem("-1"));
  m_subtestTable->setItem(row, 3, new QTableWidgetItem("-1"));
}

void TestDataDialog::onRemoveSubtest() {
  int row = m_subtestTable->currentRow();
  if (row >= 0)
    m_subtestTable->removeRow(row);
}

void TestDataDialog::onBrowseInput() {
  QString file =
      QFileDialog::getOpenFileName(this, tr("Select Input File"),
                                  QString::fromStdString(m_testsDir.string()));
  if (!file.isEmpty())
    m_inputEdit->setText(QFileInfo(file).fileName());
}

void TestDataDialog::onBrowseOutput() {
  QString file =
      QFileDialog::getOpenFileName(this, tr("Select Output File"),
                                   QString::fromStdString(m_testsDir.string()));
  if (!file.isEmpty())
    m_outputEdit->setText(QFileInfo(file).fileName());
}

void TestDataDialog::onSave() {
  m_config.name = m_nameEdit->text();
  m_config.inputFile = m_inputEdit->text();
  m_config.outputFile = m_outputEdit->text();
  m_config.evaluatorName = m_evaluatorEdit->text();
  m_config.useStdIn = m_stdInCheck->isChecked();
  m_config.useStdOut = m_stdOutCheck->isChecked();
  m_config.memoryLimit = m_memoryEdit->text().toULong();
  m_config.timeLimit = m_timeEdit->text().toFloat();
  m_config.mark = m_markEdit->text().toFloat();

  m_config.subtests.clear();
  for (int i = 0; i < m_subtestTable->rowCount(); ++i) {
    GuiSubtest st;
    st.name = m_subtestTable->item(i, 0)->text();
    st.mark = m_subtestTable->item(i, 1)->text().toFloat();
    st.timeLimit = m_subtestTable->item(i, 2)->text().toFloat();
    st.memoryLimit = m_subtestTable->item(i, 3)->text().toInt();
    m_config.subtests.push_back(st);
  }

  if (!writeProblemConfig(m_testsDir, m_problemName.toStdString(), m_config)) {
    QMessageBox::warning(this, tr("Error"),
                         tr("Failed to save Settings.cfg"));
    return;
  }
  accept();
}
