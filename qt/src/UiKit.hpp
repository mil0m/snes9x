#pragma once
// The corruption windows' shared look (docs/corruption/2026-10-31-ui-pass-plan.md and the
// qt-widgets-ui project skill): the native style, colours only from the palette, one icon
// set drawn in the palette's colours, headings instead of boxes, and the same address
// padding everywhere.
#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QLabel>
#include <QWidget>

#include <cstddef>
#include <cstdint>

class QFormLayout;

namespace ui
{

// What a piece of text is for. Each tone is a palette colour, adjusted until it reads at
// 4.5:1 or better on the window, in light and dark.
enum class Tone
{
    Normal,
    Secondary, // help and explanations: quieter than the controls, never greyed out
    Error,
    Warning,
    Ok,
};

bool isDark(const QPalette &palette);
double contrast(const QColor &a, const QColor &b); // WCAG ratio, 1..21
QColor toneColor(Tone tone, const QPalette &palette);

// Colours the label's text, and keeps it right when the palette or colour scheme changes.
void setTone(QLabel *label, Tone tone);

// A Tabler outline icon (MIT, qt/src/resources/corruption-icons) drawn in the button text
// colour of the current palette, so it follows light, dark and disabled by itself.
QIcon icon(const QString &name);
// The same picture in a fixed colour, size in device-independent pixels.
QPixmap pixmap(const QString &name, const QColor &colour, int size, double dpr = 1.0);
QStringList iconNames();

// A section heading: bold body text. Sections are set apart by space, not boxes.
QLabel *heading(const QString &text, QWidget *parent);
// Space between sections, from the style.
int sectionGap(const QWidget *widget);

// A form: labels on the left (left-aligned), fields that grow, rows that don't wrap.
QFormLayout *form(QWidget *parent = nullptr);

// A home folder path written with ~.
QString shortPath(const QString &path);

// One line that shortens itself in the middle to fit, with all of it in the tooltip.
class ElidedLabel : public QLabel
{
  public:
    explicit ElidedLabel(QWidget *parent = nullptr);
    void setFullText(const QString &text);
    QString fullText() const { return full; }
    QSize minimumSizeHint() const override;

  protected:
    void resizeEvent(QResizeEvent *event) override;

  private:
    void elide();
    QString full;
};

// One line of status: an icon, a colour and the words together, never colour alone.
class StatusLine : public QWidget
{
  public:
    explicit StatusLine(QWidget *parent = nullptr);
    void setMessage(const QString &text, Tone tone = Tone::Normal);
    QString text() const;
    Tone tone() const { return tone_; }

  protected:
    void changeEvent(QEvent *event) override;

  private:
    void refreshIcon();
    QLabel *icon_label;
    QLabel *text_label;
    Tone tone_ = Tone::Normal;
};

} // namespace ui
