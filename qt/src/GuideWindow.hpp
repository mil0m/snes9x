#pragma once
#include <QWidget>

class QTextBrowser;

// The user guide (docs/corruption/GUIDE.md, bundled), in a window of its own.
class GuideWindow : public QWidget
{
  public:
    explicit GuideWindow(QWidget *parent = nullptr);
    void showSection(const QString &heading); // scrolls to a "## heading"; empty: the top

  private:
    QTextBrowser *browser;
};
