#pragma once
#include "ContestData.h"
#include <QAbstractTableModel>
#include <QObject>

class ContestModel : public QAbstractTableModel {
  Q_OBJECT
public:
  explicit ContestModel(GuiContestData &data, QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = {}) const override;
  int columnCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  bool setData(const QModelIndex &index, const QVariant &value,
               int role) override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

  void reload();
  void addProblem(const QString &name);
  void removeProblem(int col);
  void addContestant(const QString &name);
  void removeContestant(int row);
  void eraseScoresForProblem(int problemCol);
  double totalScore(int row) const;
  QString problemName(int col) const;

signals:
  void contestModified();

private:
  GuiContestData &d;
};
