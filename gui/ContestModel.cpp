#include "ContestModel.h"

ContestModel::ContestModel(GuiContestData &data, QObject *parent)
    : QAbstractTableModel(parent), d(data) {}

int ContestModel::rowCount(const QModelIndex &) const {
  return d.contestants.size();
}

int ContestModel::columnCount(const QModelIndex &) const {
  return d.problems.size() + 2; // contestant + problems + total
}

QVariant ContestModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid())
    return {};
  if (role != Qt::DisplayRole && role != Qt::EditRole)
    return {};

  int row = index.row();
  int col = index.column();

  if (row >= d.contestants.size())
    return {};

  if (col == 0)
    return d.contestants[row];

  if (col == d.problems.size() + 1) {
    double total = 0;
    for (int i = 0; i < d.problems.size(); ++i) {
      total += d.scores.value(d.contestants[row]).value(d.problems[i], 0.0);
    }
    return total;
  }

  int pi = col - 1;
  if (pi < 0 || pi >= d.problems.size())
    return {};

  QString contestant = d.contestants[row];
  QString problem = d.problems[pi];
  double score = d.scores.value(contestant).value(problem, 0.0);
  QString verdict = d.verdicts.value(contestant + "/" + problem);

  if (verdict.isEmpty())
    return score;
  return QString("%1 %2").arg(verdict).arg(score, 0, 'f', 2);
}

bool ContestModel::setData(const QModelIndex &index, const QVariant &value,
                           int role) {
  if (role != Qt::EditRole)
    return false;

  int row = index.row();
  int col = index.column();

  if (col == 0) {
    QString old = d.contestants[row];
    if (old == value.toString())
      return false;

    auto scores = d.scores.take(old);
    auto verdictsIt = d.verdicts.begin();
    while (verdictsIt != d.verdicts.end()) {
      if (verdictsIt.key().startsWith(old + "/")) {
        QString prob = verdictsIt.key().section("/", 1);
        QString newKey = value.toString() + "/" + prob;
        d.verdicts[newKey] = verdictsIt.value();
        verdictsIt = d.verdicts.erase(verdictsIt);
      } else {
        ++verdictsIt;
      }
    }

    d.contestants[row] = value.toString();
    d.scores[value.toString()] = scores;
    emit contestModified();
    return true;
  }

  if (col > 0 && col <= d.problems.size()) {
    int pi = col - 1;
    QString contestant = d.contestants[row];
    QString problem = d.problems[pi];
    double newScore = value.toDouble();
    d.scores[contestant][problem] = newScore;
    d.verdicts[contestant + "/" + problem] = "V";
    emit contestModified();
    return true;
  }

  return false;
}

QVariant ContestModel::headerData(int section, Qt::Orientation orientation,
                                  int role) const {
  if (role != Qt::DisplayRole)
    return {};
  if (orientation == Qt::Horizontal) {
    if (section == 0)
      return "Contestant";
    if (section == d.problems.size() + 1)
      return "Total";
    int pi = section - 1;
    if (pi >= 0 && pi < d.problems.size())
      return d.problems[pi];
  } else {
    return section + 1;
  }
  return {};
}

Qt::ItemFlags ContestModel::flags(const QModelIndex &index) const {
  auto f = QAbstractTableModel::flags(index);
  f |= Qt::ItemIsEditable;
  return f;
}

void ContestModel::reload() {
  beginResetModel();
  endResetModel();
}

void ContestModel::addProblem(const QString &name) {
  if (d.problems.contains(name))
    return;
  beginInsertColumns(QModelIndex(), d.problems.size() + 1,
                     d.problems.size() + 1);
  d.problems.append(name);
  endInsertColumns();
  emit contestModified();
}

void ContestModel::removeProblem(int col) {
  if (col < 1 || col > d.problems.size())
    return;
  beginRemoveColumns(QModelIndex(), col, col);
  d.problems.removeAt(col - 1);
  endRemoveColumns();
  emit contestModified();
}

void ContestModel::addContestant(const QString &name) {
  if (d.contestants.contains(name))
    return;
  beginInsertRows(QModelIndex(), d.contestants.size(), d.contestants.size());
  d.contestants.append(name);
  endInsertRows();
  emit contestModified();
}

void ContestModel::removeContestant(int row) {
  if (row < 0 || row >= d.contestants.size())
    return;
  beginRemoveRows(QModelIndex(), row, row);
  d.contestants.removeAt(row);
  endRemoveRows();
  emit contestModified();
}

void ContestModel::eraseScoresForProblem(int problemCol) {
  if (problemCol < 1 || problemCol > d.problems.size())
    return;
  QString prob = d.problems[problemCol - 1];
  for (auto &c : d.contestants) {
    d.scores[c].remove(prob);
    d.verdicts.remove(c + "/" + prob);
  }
  emit dataChanged(index(0, problemCol),
                   index(d.contestants.size() - 1, problemCol));
  emit contestModified();
}

double ContestModel::totalScore(int row) const {
  if (row < 0 || row >= d.contestants.size())
    return 0;
  double total = 0;
  for (auto &p : d.problems)
    total += d.scores.value(d.contestants[row]).value(p, 0.0);
  return total;
}

QString ContestModel::problemName(int col) const {
  int pi = col - 1;
  if (pi < 0 || pi >= d.problems.size())
    return {};
  return d.problems[pi];
}
