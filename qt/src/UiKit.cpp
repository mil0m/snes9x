#include "UiKit.hpp"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIconEngine>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPixmapCache>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSlider>
#include <QStyle>

#include <algorithm>
#include <cmath>

// The icons live in a resource file of their own, which the app and the tests both link.
static void init_icons()
{
    static bool done = false;
    if (!done)
    {
        Q_INIT_RESOURCE(corruption_icons);
        done = true;
    }
}

namespace ui
{

namespace
{

double luminance(const QColor &c)
{
    auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    QColor rgb = c.toRgb();
    return 0.2126 * channel(rgb.redF()) + 0.7152 * channel(rgb.greenF()) + 0.0722 * channel(rgb.blueF());
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    QColor x = a.toRgb(), y = b.toRgb();
    return QColor::fromRgbF(float(x.redF() + (y.redF() - x.redF()) * t), float(x.greenF() + (y.greenF() - x.greenF()) * t),
                            float(x.blueF() + (y.blueF() - x.blueF()) * t));
}

// Moves a colour towards the text colour until it reads on the background.
QColor readable(QColor c, const QPalette &pal)
{
    QColor bg = pal.color(QPalette::Window), text = pal.color(QPalette::WindowText);
    for (int i = 0; i < 20 && contrast(c, bg) < 4.5; i++)
        c = mix(c, text, 0.15);
    return c;
}

const char *kTone = "ui_tone";
const char *kBusy = "ui_tone_busy";

// Re-colours toned labels whenever their surroundings' palette changes.
class ToneKeeper : public QObject
{
  public:
    static ToneKeeper &get()
    {
        static ToneKeeper keeper;
        return keeper;
    }
    static void apply(QLabel *label)
    {
        if (label->property(kBusy).toBool())
            return;
        label->setProperty(kBusy, true);
        auto tone = Tone(label->property(kTone).toInt());
        QPalette source = label->parentWidget() ? label->parentWidget()->palette() : QApplication::palette();
        QPalette pal = label->palette();
        pal.setColor(QPalette::WindowText, toneColor(tone, source));
        label->setPalette(pal);
        label->setProperty(kBusy, false);
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange ||
            event->type() == QEvent::ParentChange || event->type() == QEvent::StyleChange)
            if (auto label = qobject_cast<QLabel *>(watched))
                apply(label);
        return false;
    }
};

const QStringList kIcons = { "alert-circle", "alert-triangle", "arrow-back-up", "bolt", "chevron-down", "circle-check", "copy",
                             "device-floppy", "dice-5", "eraser", "file-export", "file-plus", "folder-open",
                             "grid-dots", "help-circle", "history", "info-circle", "pencil", "pin", "pinned-off",
                             "player-stop", "plus", "refresh", "restore", "search", "tag", "trash", "zoom-in",
                             "zoom-out" };

QByteArray svg_source(const QString &name)
{
    init_icons();
    static QHash<QString, QByteArray> cache;
    auto it = cache.find(name);
    if (it != cache.end())
        return *it;
    QFile file(":/corruption-icons/" + name + ".svg");
    QByteArray data;
    if (file.open(QIODevice::ReadOnly))
        data = file.readAll();
    // Tabler's 2 px stroke is heavy at 16 px; 1.75 sits closer to the system's symbols.
    data.replace("stroke-width=\"2\"", "stroke-width=\"1.75\"");
    cache.insert(name, data);
    return data;
}

QImage render(const QString &name, const QColor &colour, QSize pixels)
{
    QByteArray data = svg_source(name);
    if (data.isEmpty() || pixels.isEmpty())
        return {};
    data.replace("currentColor", colour.name(QColor::HexRgb).toLatin1());
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "svg");
    reader.setScaledSize(pixels);
    QImage img = reader.read();
    if (img.isNull())
        return {};
    if (colour.alpha() < 255) // a translucent colour (disabled text) fades the whole picture
    {
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter p(&img);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.fillRect(img.rect(), QColor(0, 0, 0, colour.alpha()));
    }
    return img;
}

// Draws the icon at paint time in the current palette's colour for its mode.
class TintedIcon : public QIconEngine
{
  public:
    explicit TintedIcon(QString name) : name(std::move(name)) {}
    QIconEngine *clone() const override { return new TintedIcon(name); }
    QString key() const override { return "ui-tinted"; }
    QSize actualSize(const QSize &size, QIcon::Mode, QIcon::State) override { return size; }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        double dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, dpr));
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State, qreal scale) override
    {
        QPalette pal = QApplication::palette();
        QColor colour = mode == QIcon::Disabled ? pal.color(QPalette::Disabled, QPalette::ButtonText)
                        : mode == QIcon::Selected ? pal.color(QPalette::HighlightedText)
                                                  : pal.color(QPalette::Active, QPalette::ButtonText);
        if (mode == QIcon::Disabled && colour == pal.color(QPalette::Active, QPalette::ButtonText))
            colour.setAlpha(110); // some palettes don't grey disabled text
        QSize pixels = size * scale;
        QString cache_key = QString("ui-icon:%1:%2:%3x%4").arg(name, colour.name(QColor::HexArgb)).arg(pixels.width()).arg(pixels.height());
        QPixmap pm;
        if (!QPixmapCache::find(cache_key, &pm))
        {
            pm = QPixmap::fromImage(render(name, colour, pixels));
            QPixmapCache::insert(cache_key, pm);
        }
        pm.setDevicePixelRatio(scale);
        return pm;
    }
    bool isNull() override { return svg_source(name).isEmpty(); }

  private:
    QString name;
};

} // namespace

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < palette.color(QPalette::WindowText).lightness();
}

double contrast(const QColor &a, const QColor &b)
{
    double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor toneColor(Tone tone, const QPalette &pal)
{
    bool dark = isDark(pal);
    QColor text = pal.color(QPalette::WindowText), window = pal.color(QPalette::Window);
    switch (tone)
    {
    case Tone::Normal: return text;
    case Tone::Secondary: return readable(mix(text, window, 0.38), pal);
    case Tone::Error: return readable(dark ? QColor(255, 125, 115) : QColor(196, 32, 24), pal);
    case Tone::Warning: return readable(dark ? QColor(240, 170, 70) : QColor(160, 88, 0), pal);
    case Tone::Ok: return readable(dark ? QColor(100, 205, 120) : QColor(24, 120, 48), pal);
    }
    return text;
}

void setTone(QLabel *label, Tone tone)
{
    label->setProperty(kTone, int(tone));
    label->removeEventFilter(&ToneKeeper::get());
    label->installEventFilter(&ToneKeeper::get());
    ToneKeeper::apply(label);
}

QIcon icon(const QString &name)
{
    return QIcon(new TintedIcon(name));
}

QPixmap pixmap(const QString &name, const QColor &colour, int size, double dpr)
{
    QPixmap pm = QPixmap::fromImage(render(name, colour, QSize(size, size) * dpr));
    pm.setDevicePixelRatio(dpr);
    return pm;
}

QStringList iconNames()
{
    return kIcons;
}

QLabel *heading(const QString &text, QWidget *parent)
{
    auto label = new QLabel(text, parent);
    label->setObjectName("heading");
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    label->setAccessibleName(text);
    return label;
}

int sectionGap(const QWidget *widget)
{
    QStyle *style = widget ? widget->style() : QApplication::style();
    int gap = style->pixelMetric(QStyle::PM_LayoutVerticalSpacing, nullptr, widget);
    if (gap < 0) // the Mac style spaces per control type instead
        gap = style->layoutSpacing(QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Vertical, nullptr, widget);
    if (gap < 0)
        gap = 6;
    return std::clamp(gap * 2 + 4, 12, 24);
}

QLayout *row(QWidget *w)
{
    auto r = new QHBoxLayout;
    r->setContentsMargins(0, 0, 0, 0);
    r->addWidget(w);
    return r;
}

void alignForms(QWidget *root)
{
    QList<QWidget *> labels;
    int widest = 0;
    for (auto f : root->findChildren<QFormLayout *>())
        for (int i = 0; i < f->rowCount(); i++)
            if (auto item = f->itemAt(i, QFormLayout::LabelRole); item && item->widget())
            {
                labels << item->widget();
                widest = std::max(widest, item->widget()->sizeHint().width());
            }
    for (auto l : labels)
        l->setMinimumWidth(widest);
}

QFormLayout *form(QWidget *parent)
{
    auto f = new QFormLayout(parent);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    f->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    // Left, like the headings above them: right-aligned labels leave a ragged edge.
    f->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    f->setRowWrapPolicy(QFormLayout::DontWrapRows);
    return f;
}

QStringList audit(QWidget *root)
{
    QStringList issues;
    auto describe = [](QWidget *w) {
        QString name = w->objectName().isEmpty() ? QString("(unnamed)") : w->objectName();
        return QString("%1 %2").arg(w->metaObject()->className(), name);
    };
    QSet<QWidget *> labelled;
    for (auto label : root->findChildren<QLabel *>())
        if (label->buddy())
            labelled.insert(label->buddy());
    auto inside_compound = [](QWidget *w) { // a spin box's or combo box's own parts
        for (auto p = w->parentWidget(); p; p = p->parentWidget())
            if (qobject_cast<QAbstractSpinBox *>(p) || qobject_cast<QComboBox *>(p))
                return true;
        return false;
    };
    auto named_here_or_above = [root](QWidget *w) { // a compound widget named as a whole
        for (auto p = w; p && p != root->parentWidget(); p = p->parentWidget())
            if (!p->accessibleName().isEmpty())
                return true;
        return false;
    };
    for (auto b : root->findChildren<QAbstractButton *>())
    {
        if (!b->text().isEmpty() || b->icon().isNull() || inside_compound(b) ||
            b->objectName().startsWith("qt_")) // Qt's own, like a dock's title buttons
            continue;
        if (b->accessibleName().isEmpty())
            issues << describe(b) + ": icon button without an accessible name";
        if (b->toolTip().isEmpty())
            issues << describe(b) + ": icon button without a tooltip";
    }
    for (auto w : root->findChildren<QWidget *>())
    {
        bool input = qobject_cast<QLineEdit *>(w) || qobject_cast<QComboBox *>(w) ||
                     qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QSlider *>(w);
        if (!input || inside_compound(w) || labelled.contains(w) || named_here_or_above(w))
            continue;
        issues << describe(w) + ": input with no label or accessible name";
    }
    return issues;
}

QString shortPath(const QString &path)
{
    QString home = QDir::homePath();
    if (!home.isEmpty() && (path == home || path.startsWith(home + "/")))
        return "~" + path.mid(home.size());
    return QDir::toNativeSeparators(path);
}

ElidedLabel::ElidedLabel(QWidget *parent)
    : QLabel(parent)
{
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setTextInteractionFlags(Qt::NoTextInteraction);
}

void ElidedLabel::setFullText(const QString &text)
{
    full = text;
    setToolTip(text);
    elide();
}

QSize ElidedLabel::minimumSizeHint() const
{
    return { fontMetrics().horizontalAdvance("M") * 4, QLabel::minimumSizeHint().height() };
}

void ElidedLabel::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    elide();
}

void ElidedLabel::elide()
{
    QString shown = fontMetrics().elidedText(full, Qt::ElideMiddle, std::max(width() - 2, 0));
    if (shown != text())
        setText(shown);
}

StatusLine::StatusLine(QWidget *parent)
    : QWidget(parent)
{
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    icon_label = new QLabel(this);
    icon_label->setObjectName("status_icon");
    icon_label->setAlignment(Qt::AlignTop);
    text_label = new QLabel(this);
    text_label->setObjectName("status_text");
    text_label->setWordWrap(true);
    text_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(icon_label, 0, Qt::AlignTop);
    layout->addWidget(text_label, 1);
    setMessage(QString());
}

void StatusLine::setMessage(const QString &text, Tone tone)
{
    tone_ = tone;
    text_label->setText(text);
    setTone(text_label, tone);
    setVisible(!text.isEmpty());
    refreshIcon();
}

QString StatusLine::text() const
{
    return text_label->text();
}

void StatusLine::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        refreshIcon();
}

void StatusLine::refreshIcon()
{
    const char *name = tone_ == Tone::Error     ? "alert-circle"
                       : tone_ == Tone::Warning ? "alert-triangle"
                       : tone_ == Tone::Ok      ? "circle-check"
                                                : "info-circle";
    int size = text_label->fontMetrics().height();
    icon_label->setPixmap(pixmap(name, toneColor(tone_, palette()), size, devicePixelRatioF()));
    icon_label->setAccessibleName(tone_ == Tone::Error ? tr("Error") : tone_ == Tone::Warning ? tr("Warning") : QString());
}

} // namespace ui
