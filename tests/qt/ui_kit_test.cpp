// The shared look: tones that stay readable in light and dark, icons drawn in the
// palette's colours, headings, forms and address padding.
#include "doctest/doctest.h"

#include "UiKit.hpp"

#include <QApplication>
#include <QDir>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QPixmap>
#include <QWidget>

namespace
{

QPalette light_palette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(236, 236, 236));
    p.setColor(QPalette::WindowText, Qt::black);
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::Text, Qt::black);
    p.setColor(QPalette::Button, QColor(236, 236, 236));
    p.setColor(QPalette::ButtonText, Qt::black);
    p.setColor(QPalette::PlaceholderText, QColor(160, 160, 160));
    return p;
}

// The same dark palette main.cpp uses for --dark.
QPalette dark_palette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(53, 53, 53));
    p.setColor(QPalette::WindowText, Qt::white);
    p.setColor(QPalette::Base, QColor(25, 25, 25));
    p.setColor(QPalette::Text, Qt::white);
    p.setColor(QPalette::Button, QColor(53, 53, 53));
    p.setColor(QPalette::ButtonText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(127, 127, 127));
    return p;
}

// Average colour of the opaque pixels.
QColor ink(const QPixmap &pm)
{
    QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    double r = 0, g = 0, b = 0, w = 0;
    for (int y = 0; y < img.height(); y++)
        for (int x = 0; x < img.width(); x++)
        {
            QColor c = img.pixelColor(x, y);
            double a = c.alphaF();
            r += c.redF() * a;
            g += c.greenF() * a;
            b += c.blueF() * a;
            w += a;
        }
    if (w == 0)
        return {};
    return QColor::fromRgbF(float(r / w), float(g / w), float(b / w));
}

} // namespace

TEST_CASE("every tone reads at 4.5:1 or better on the window, light and dark")
{
    for (auto pal : { light_palette(), dark_palette() })
        for (auto tone : { ui::Tone::Normal, ui::Tone::Secondary, ui::Tone::Error, ui::Tone::Warning, ui::Tone::Ok })
        {
            CAPTURE(int(tone));
            CAPTURE(ui::isDark(pal));
            CHECK(ui::contrast(ui::toneColor(tone, pal), pal.color(QPalette::Window)) >= 4.5);
        }
    // Secondary is quieter than the body text, but not the faint placeholder grey.
    auto pal = light_palette();
    auto window = pal.color(QPalette::Window);
    CHECK(ui::contrast(ui::toneColor(ui::Tone::Secondary, pal), window) <
          ui::contrast(pal.color(QPalette::WindowText), window));
    CHECK(ui::contrast(ui::toneColor(ui::Tone::Secondary, pal), window) >
          ui::contrast(pal.color(QPalette::PlaceholderText), window));
    CHECK_FALSE(ui::isDark(light_palette()));
    CHECK(ui::isDark(dark_palette()));
}

TEST_CASE("a toned label follows a switch between light and dark")
{
    QWidget parent;
    parent.setPalette(light_palette());
    auto label = new QLabel("hint", &parent);
    ui::setTone(label, ui::Tone::Error);
    QColor in_light = label->palette().color(QPalette::WindowText);
    CHECK(in_light == ui::toneColor(ui::Tone::Error, light_palette()));
    parent.setPalette(dark_palette());
    QColor in_dark = label->palette().color(QPalette::WindowText);
    CHECK(in_dark == ui::toneColor(ui::Tone::Error, dark_palette()));
    CHECK(in_light != in_dark);
    ui::setTone(label, ui::Tone::Normal); // back to the plain text colour
    CHECK(label->palette().color(QPalette::WindowText) == dark_palette().color(QPalette::WindowText));
}

TEST_CASE("icons exist and are drawn in the palette's text colour")
{
    REQUIRE(ui::iconNames().size() >= 20);
    for (auto &name : ui::iconNames())
    {
        CAPTURE(name.toStdString());
        CHECK_FALSE(ui::icon(name).isNull());
        CHECK_FALSE(ui::pixmap(name, Qt::black, 16).isNull());
    }
    auto old = QApplication::palette();
    QApplication::setPalette(light_palette());
    auto on_light = ink(ui::icon("bolt").pixmap(QSize(32, 32)));
    QApplication::setPalette(dark_palette());
    auto on_dark = ink(ui::icon("bolt").pixmap(QSize(32, 32)));
    QApplication::setPalette(old);
    CHECK(on_light.lightnessF() < 0.3); // dark ink on light
    CHECK(on_dark.lightnessF() > 0.7);  // light ink on dark
    // A fixed colour, for status icons.
    auto red = ink(ui::pixmap("alert-circle", QColor(200, 0, 0), 32));
    CHECK(red.red() > 150);
    CHECK(red.green() < 60);
    // A sharp picture on a 2x screen.
    CHECK(ui::pixmap("bolt", Qt::black, 16, 2.0).width() == 32);
}

TEST_CASE("forms put labels on the left and let fields grow")
{
    QWidget w;
    auto form = ui::form(&w);
    CHECK(form->fieldGrowthPolicy() == QFormLayout::AllNonFixedFieldsGrow);
    CHECK((form->formAlignment() & Qt::AlignLeft));
    CHECK(form->rowWrapPolicy() == QFormLayout::DontWrapRows);
}

TEST_CASE("a heading is bold and named for tests and screen readers")
{
    QWidget w;
    auto h = ui::heading("Rules", &w);
    CHECK(h->font().bold());
    CHECK(h->text() == "Rules");
    CHECK(h->accessibleName() == "Rules");
}

TEST_CASE("the section gap is wider than the gap between controls")
{
    QWidget w;
    CHECK(ui::sectionGap(&w) >= 12);
    CHECK(ui::sectionGap(&w) <= 24);
}

TEST_CASE("a long path shows its start and end, with home as ~, and the whole of it in the tooltip")
{
    QWidget w;
    auto label = new ui::ElidedLabel(&w);
    QString path = QDir::homePath() + "/Documents/Some/Very/Long/Folder/Names/Snes9x Corruptions";
    label->setFullText(QObject::tr("Library: %1").arg(ui::shortPath(path)));
    w.resize(2100, 100);
    w.show();
    label->resize(160, label->sizeHint().height());
    QApplication::processEvents();
    CHECK(ui::shortPath(path).startsWith("~/Documents"));
    CHECK(label->text().contains(QChar(0x2026))); // …
    CHECK(label->text().endsWith("ions")); // the folder's own name, at the end
    CHECK(label->toolTip().contains("~/Documents/Some/Very/Long"));
    label->resize(2000, label->height());
    QApplication::processEvents();
    CHECK_FALSE(label->text().contains(QChar(0x2026)));
    CHECK(label->minimumSizeHint().width() < 100); // it never holds the panel wide
}

TEST_CASE("the audit finds icon buttons without a name or tooltip, and inputs nobody labels")
{
    QWidget w;
    auto layout = new QVBoxLayout(&w);
    auto bare = new QToolButton(&w);
    bare->setIcon(ui::icon("trash"));
    layout->addWidget(bare);
    auto edit = new QLineEdit(&w);
    layout->addWidget(edit);
    auto spin = new QSpinBox(&w); // its own line edit is part of it, not a second input
    layout->addWidget(spin);
    auto issues = ui::audit(&w);
    CHECK(issues.size() == 4); // the button: no name, no tooltip; the edit and the spin box: no label
    bare->setToolTip("Remove the selected rule");
    bare->setAccessibleName("Remove the selected rule");
    auto label = new QLabel("Seed", &w);
    label->setBuddy(edit);
    spin->setAccessibleName("Zoom");
    CHECK(ui::audit(&w).isEmpty());
    // A labelled form row is labelled.
    QWidget f;
    auto form = ui::form(&f);
    form->addRow("Name", new QLineEdit(&f));
    CHECK(ui::audit(&f).isEmpty());
}

TEST_CASE("forms in different sections share one label width, so their fields line up")
{
    QWidget w;
    auto v = new QVBoxLayout(&w);
    auto a = ui::form();
    auto b = ui::form();
    v->addLayout(a);
    v->addLayout(b);
    a->addRow("Name", new QLineEdit(&w));
    b->addRow("Bytes each time", new QLineEdit(&w));
    ui::alignForms(&w);
    w.show();
    QApplication::processEvents();
    auto field = [](QFormLayout *f) { return f->itemAt(0, QFormLayout::FieldRole)->widget()->x(); };
    CHECK(field(a) == field(b));
    CHECK(ui::row(new QLineEdit(&w))->count() == 1);
}
