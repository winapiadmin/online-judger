#include "MainWindow.h"
#include "CompilerDialog.h"
#include "ContestData.h"
#include "JudgeBackend.h"
#include "TestDataDialog.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QVBoxLayout>
#include <filesystem>

namespace fs = std::filesystem;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(tr("Online Judger"));
  resize(900, 600);

  auto *central = new QWidget();
  setCentralWidget(central);
  auto *mainLayout = new QVBoxLayout(central);

  auto *pathLayout = new QHBoxLayout();
  auto *testsBtn = new QPushButton(tr("Select Tests Folder..."));
  auto *subsBtn = new QPushButton(tr("Select Submissions Folder..."));
  connect(testsBtn, &QPushButton::clicked, this, &MainWindow::onSelectTests);
  connect(subsBtn, &QPushButton::clicked, this,
          &MainWindow::onSelectSubmissions);
  pathLayout->addWidget(testsBtn);
  pathLayout->addWidget(subsBtn);
  pathLayout->addStretch();
  mainLayout->addLayout(pathLayout);

  m_table = new QTableView();
  m_model = new ContestModel(m_data, this);
  m_table->setModel(m_model);
  m_table->setContextMenuPolicy(Qt::CustomContextMenu);
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->verticalHeader()->setDefaultSectionSize(30);
  connect(m_table, &QTableView::customContextMenuRequested, this,
          &MainWindow::onTableContextMenu);
  mainLayout->addWidget(m_table);

  auto *btnLayout = new QHBoxLayout();
  auto *addContBtn = new QPushButton(tr("Add Contestant"));
  auto *remContBtn = new QPushButton(tr("Remove Contestant"));
  auto *addProbBtn = new QPushButton(tr("Add Problem"));
  connect(addContBtn, &QPushButton::clicked, this,
          &MainWindow::onAddContestant);
  connect(remContBtn, &QPushButton::clicked, this,
          &MainWindow::onRemoveContestant);
  connect(addProbBtn, &QPushButton::clicked, this, &MainWindow::onAddProblem);
  btnLayout->addWidget(addContBtn);
  btnLayout->addWidget(remContBtn);
  btnLayout->addWidget(addProbBtn);
  btnLayout->addStretch();
  mainLayout->addLayout(btnLayout);

  auto *testLayout = new QHBoxLayout();
  m_progressBar = new QProgressBar();
  m_progressBar->setVisible(false);
  m_cancelBtn = new QPushButton(tr("Cancel"));
  m_cancelBtn->setVisible(false);
  connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancelTest);
  testLayout->addWidget(m_progressBar, 1);
  testLayout->addWidget(m_cancelBtn);
  mainLayout->addLayout(testLayout);

  auto *menuBar = this->menuBar();
  auto *fileMenu = menuBar->addMenu(tr("&File"));

  fileMenu->addAction(tr("&Import Contest (.contest)..."), this,
                      &MainWindow::onLoadContest);
  fileMenu->addAction(tr("&Export Contest (.contest)..."), this,
                      &MainWindow::onSaveContest);
  fileMenu->addSeparator();
  fileMenu->addAction(tr("Export to &CSV..."), this, &MainWindow::onExportCSV);
  fileMenu->addSeparator();

  auto *toolsMenu = menuBar->addMenu(tr("&Tools"));
  toolsMenu->addAction(tr("&Configure Compilers..."), this,
                       &MainWindow::onConfigureCompilers);

  statusBar()->showMessage(tr("Ready"));

  // Load saved config (paths + compilers from last session / Themis.cfg)
  m_config.load();
  syncConfigToData();
  if (!m_data.testsPath.isEmpty() && !m_data.submissionsPath.isEmpty()) {
    ::loadFromFolders(m_data);
    m_model->reload();
    statusBar()->showMessage(tr("Loaded %1 problems, %2 contestants")
                                 .arg(m_data.problems.size())
                                 .arg(m_data.contestants.size()));
  }
}

MainWindow::~MainWindow() {
  syncDataToConfig();
  m_config.save();
}

void MainWindow::onSelectTests() {
  QString dir =
      QFileDialog::getExistingDirectory(this, tr("Select Tests Folder"));
  if (dir.isEmpty())
    return;
  m_data.testsPath = dir;
  ::loadFromFolders(m_data);
  m_model->reload();
  syncDataToConfig();
  m_config.save();
  statusBar()->showMessage(tr("Loaded %1 problems, %2 contestants")
                               .arg(m_data.problems.size())
                               .arg(m_data.contestants.size()));
}

void MainWindow::onSelectSubmissions() {
  QString dir =
      QFileDialog::getExistingDirectory(this, tr("Select Submissions Folder"));
  if (dir.isEmpty())
    return;
  m_data.submissionsPath = dir;
  ::loadFromFolders(m_data);
  m_model->reload();
  syncDataToConfig();
  m_config.save();
  statusBar()->showMessage(tr("Loaded %1 problems, %2 contestants")
                               .arg(m_data.problems.size())
                               .arg(m_data.contestants.size()));
}

void MainWindow::onTableContextMenu(const QPoint &pos) {
  auto index = m_table->indexAt(pos);
  if (!index.isValid())
    return;

  int col = index.column();
  if (col < 1 || col > m_data.problems.size())
    return;

  QString prob = m_model->problemName(col);

  QMenu menu(this);
  menu.addAction(tr("Re-test: %1").arg(prob), [this, col]() { onReTest(col); });
  menu.addAction(tr("Erase scores: %1").arg(prob),
                 [this, col]() { onEraseScores(col); });
  menu.addAction(tr("Modify test data: %1").arg(prob),
                 [this, col]() { onModifyTestData(col); });
  menu.addAction(tr("Detailed result: %1").arg(prob),
                 [this, col]() { onDetailedResult(col); });
  menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void MainWindow::onReTest(int problemCol) {
  if (m_testRunning) {
    QMessageBox::information(this, tr("Testing"),
                             tr("A test is already in progress."));
    return;
  }
  QString problem = m_model->problemName(problemCol);
  startBatchReTest(problem);
}

void MainWindow::onCancelTest() { m_cancelRequested = true; }

void MainWindow::startBatchReTest(const QString &problem) {
  if (m_data.submissionsPath.isEmpty() || m_data.testsPath.isEmpty()) {
    QMessageBox::warning(this, tr("Error"),
                         tr("Select tests and submissions folders first."));
    return;
  }

  QStringList contestants = m_data.contestants;
  int total = contestants.size();
  if (total == 0)
    return;

  // Build testcases map and configuration for judge()
  fs::path testsDir(m_data.testsPath.toStdString());
  auto testcases = buildTestcasesMap(testsDir);
  Configuration conf = m_config.configuration();
  conf.environment.contestHouse = "/tmp";

  // Evaluator shared libraries: current working directory
  fs::path judgerPath = fs::current_path();

  QString problemStd = problem;
  std::string problemStr = problemStd.toStdString();
  fs::path subsDir(m_data.submissionsPath.toStdString());

  m_cancelRequested = false;
  m_testRunning = true;
  m_progressBar->setRange(0, total);
  m_progressBar->setValue(0);
  m_progressBar->setVisible(true);
  m_cancelBtn->setVisible(true);

  auto *thread = QThread::create([this, contestants, problemStr, testcases,
                                  conf, judgerPath, subsDir, total]() {
    for (int i = 0; i < total; ++i) {
      if (m_cancelRequested)
        break;

      std::string user = contestants[i].toStdString();
      judge(subsDir, fs::path(m_data.testsPath.toStdString()), problemStr, user,
            conf, testcases, judgerPath);

      QMetaObject::invokeMethod(
          this, [this, i]() { m_progressBar->setValue(i + 1); });
    }

    // Collect scores
    auto scores = getScores();

    QMetaObject::invokeMethod(this, [this, scores]() {
      for (auto &[key, verdict] : scores) {
        auto &[user, problem] = key;
        auto &[verdictStr, points] = verdict;
        QString qUser = QString::fromStdString(user);
        QString qProb = QString::fromStdString(problem);
        m_data.scores[qUser][qProb] = points;
        m_data.verdicts[qUser + "/" + qProb] =
            QString::fromStdString(verdictStr);
      }
      m_testRunning = false;
      m_progressBar->setVisible(false);
      m_cancelBtn->setVisible(false);
      m_model->reload();
      statusBar()->showMessage(tr("Re-test complete"), 5000);
    });
  });
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
}

void MainWindow::onEraseScores(int problemCol) {
  QString problem = m_model->problemName(problemCol);
  auto ret = QMessageBox::question(
      this, tr("Erase Scores"),
      tr("Erase all scores for problem '%1'?").arg(problem));
  if (ret == QMessageBox::Yes) {
    m_model->eraseScoresForProblem(problemCol);
  }
}

void MainWindow::onModifyTestData(int problemCol) {
  QString problem = m_model->problemName(problemCol);
  if (m_data.testsPath.isEmpty()) {
    QMessageBox::warning(this, tr("Error"), tr("No tests folder selected"));
    return;
  }
  TestDataDialog dlg(fs::path(m_data.testsPath.toStdString()), problem, this);
  dlg.exec();
}

void MainWindow::onDetailedResult(int problemCol) {
  QString problem = m_model->problemName(problemCol);
  QDialog dlg(this);
  dlg.setWindowTitle(tr("Detailed Result - %1").arg(problem));
  dlg.resize(600, 400);

  auto *layout = new QVBoxLayout(&dlg);
  auto *textEdit = new QLabel();
  textEdit->setWordWrap(true);
  textEdit->setAlignment(Qt::AlignTop | Qt::AlignLeft);

  QString details;
  for (auto &contestant : m_data.contestants) {
    QString key = contestant + "/" + problem;
    double score = m_data.scores.value(contestant).value(problem, 0.0);
    QString verdict = m_data.verdicts.value(key, "-");
    details += QString("%1: %2 (%3)\n")
                   .arg(contestant)
                   .arg(score, 0, 'f', 2)
                   .arg(verdict);
  }
  textEdit->setText(details);
  layout->addWidget(textEdit);

  auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  layout->addWidget(btnBox);

  dlg.exec();
}

void MainWindow::onAddContestant() {
  bool ok;
  QString name =
      QInputDialog::getText(this, tr("Add Contestant"), tr("Contestant name:"),
                            QLineEdit::Normal, {}, &ok);
  if (ok && !name.isEmpty()) {
    m_model->addContestant(name);
  }
}

void MainWindow::onRemoveContestant() {
  auto idx = m_table->currentIndex();
  if (!idx.isValid())
    return;
  m_model->removeContestant(idx.row());
}

void MainWindow::onAddProblem() {
  bool ok;
  QString name = QInputDialog::getText(
      this, tr("Add Problem"), tr("Problem name:"), QLineEdit::Normal, {}, &ok);
  if (ok && !name.isEmpty()) {
    m_model->addProblem(name);
  }
}

void MainWindow::onLoadContest() {
  QString path = QFileDialog::getOpenFileName(
      this, tr("Import Contest"), {}, tr("Contest Files (*.contest);;All (*)"));
  if (path.isEmpty())
    return;

  QTemporaryDir tmpDir;
  if (!tmpDir.isValid()) {
    QMessageBox::warning(this, tr("Error"),
                         tr("Failed to create temp directory"));
    return;
  }

  if (!importContestZip(path, tmpDir.path(), m_data)) {
    QMessageBox::warning(this, tr("Error"),
                         tr("Failed to import .contest file"));
    return;
  }
  m_contestFilePath = path;
  m_model->reload();
  statusBar()->showMessage(tr("Imported: %1").arg(path));
}

void MainWindow::onSaveContest() {
  QString path = QFileDialog::getSaveFileName(
      this, tr("Export Contest"),
      m_contestFilePath.isEmpty() ? QString() : m_contestFilePath,
      tr("Contest Files (*.contest);;All (*)"));
  if (path.isEmpty())
    return;
  if (!exportContestZip(path, m_data)) {
    QMessageBox::warning(this, tr("Error"),
                         tr("Failed to export .contest file"));
    return;
  }
  m_contestFilePath = path;
  statusBar()->showMessage(tr("Exported: %1").arg(path));
}

void MainWindow::onExportCSV() {
  QString path = QFileDialog::getSaveFileName(this, tr("Export CSV"), {},
                                              tr("CSV Files (*.csv)"));
  if (path.isEmpty())
    return;
  if (!exportCSV(path, m_data)) {
    QMessageBox::warning(this, tr("Error"), tr("Failed to export CSV"));
    return;
  }
  statusBar()->showMessage(tr("Exported: %1").arg(path));
}

void MainWindow::onConfigureCompilers() {
  CompilerDialog dlg(m_data.compilers, this);
  if (dlg.exec() == QDialog::Accepted) {
    syncDataToConfig();
    m_config.save();
  }
}

void MainWindow::syncConfigToData() {
  m_data.testsPath = QString::fromStdString(m_config.testsPath());
  m_data.submissionsPath = QString::fromStdString(m_config.submissionsPath());
  m_data.compilers = toGuiCompilers(m_config.compilers());
}

void MainWindow::syncDataToConfig() {
  m_config.setTestsPath(m_data.testsPath.toStdString());
  m_config.setSubmissionsPath(m_data.submissionsPath.toStdString());
  m_config.compilers() = fromGuiCompilers(m_data.compilers);
}
