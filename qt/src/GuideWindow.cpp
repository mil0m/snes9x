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
    layout->setContentsMargins(0, 0, 0, 0); // a content view goes to the window's edge
    browser = new QTextBrowser(this);
    browser->setFrameShape(QFrame::NoFrame);
    browser->document()->setDocumentMargin(16);
    browser->setObjectName("guide_text");
    browser->setOpenExternalLinks(true);
    QFile file(":/corruption-guide/GUIDE.md");
    if (file.open(QIODevice::ReadOnly))
    {
        browser->setMarkdown(QString::fromUtf8(file.readAll()));
        // Room above each heading, so a section starts visibly apart from the table before it.
        QTextCursor cursor(browser->document());
        for (auto block = browser->document()->begin(); block.isValid(); block = block.next())
            if (int level = block.blockFormat().headingLevel(); level > 0)
            {
                auto format = block.blockFormat();
                format.setTopMargin(level == 1 ? 0 : 22);
                format.setBottomMargin(6);
                cursor.setPosition(block.position());
                cursor.setBlockFormat(format);
            }
    }
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
