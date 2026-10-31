#include "GuideWindow.hpp"

#include <QFile>
#include <QAbstractTextDocumentLayout>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

GuideWindow::GuideWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Corruption Guide"));
    resize(720, 760);
    auto layout = new QVBoxLayout(this);
    browser = new QTextBrowser(this);
    browser->setObjectName("guide_text");
    browser->setOpenExternalLinks(true);
    QFile file(":/corruption-guide/GUIDE.md");
    if (file.open(QIODevice::ReadOnly))
        browser->setMarkdown(QString::fromUtf8(file.readAll()));
    else
        browser->setPlainText(tr("The guide is missing from this build. It's docs/corruption/GUIDE.md in the source."));
    layout->addWidget(browser);
}

void GuideWindow::showSection(const QString &heading)
{
    if (heading.isEmpty())
        return browser->moveCursor(QTextCursor::Start);
    // Headings become plain text blocks; find the one with this text.
    for (auto block = browser->document()->begin(); block.isValid(); block = block.next())
        if (block.blockFormat().headingLevel() > 0 && block.text() == heading)
        {
            // The heading at the top of the window, not just somewhere on screen.
            auto top = browser->document()->documentLayout()->blockBoundingRect(block).top();
            browser->verticalScrollBar()->setValue(int(top));
            return;
        }
}
