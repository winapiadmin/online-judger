#include "MainWindow.h"
#include <QApplication>
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>

int main(int argc, char *argv[]) {
  plog::ColorConsoleAppender<plog::TxtFormatter> nullAppender;
  plog::init(plog::none, &nullAppender);

  QApplication app(argc, argv);
  app.setApplicationName("Online Judger");
  app.setOrganizationName("OnlineJudger");

  MainWindow w;
  w.show();

  return app.exec();
}
