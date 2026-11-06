#include "ExploreCharts.hpp"

#include "UiKit.hpp"

#include <QEvent>
#include <QHelpEvent>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

using namespace corrupt;

namespace
{
constexpr int kRow = 22, kTitle = 22, kLegend = 20, kPlot = 150, kGap = 14;

QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

QString count(uint32_t n, const char *one, const char *many)
{
    return n == 1 ? QObject::tr(one) : QObject::tr(many).arg(QLocale().toString(n));
}
} // namespace

ExploreCharts::ExploreCharts(QWidget *parent) : QWidget(parent)
{
    setObjectName("explore_charts");
    setAccessibleName(tr("What the exploration did"));
    setMouseTracking(true);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void ExploreCharts::setExploration(const Exploration &e)
{
    st = stats_of(e);
    families.clear();
    for (auto &t : e.tried)
        families.push_back(t.family);
    tips.clear();
    updateGeometry();
    update();
}

int ExploreCharts::kindsHeight() const
{
    return kTitle + kLegend + int(st.kinds.size()) * kRow + 6;
}

int ExploreCharts::heightForWidth(int) const
{
    if (st.kinds.empty())
        return 0;
    int h = kindsHeight();
    h += st.detailed ? 2 * (kGap + kTitle + kLegend + kPlot + 30) : kGap + kTitle;
    return h;
}

QSize ExploreCharts::sizeHint() const
{
    return { 600, heightForWidth(600) };
}

void ExploreCharts::draw(QPainter *p) const
{
    tips.clear();
    if (st.kinds.empty())
        return;
    const QPalette &pal = palette();
    QColor ink = pal.color(QPalette::WindowText), quiet = ui::toneColor(ui::Tone::Secondary, pal);
    QColor visible = pal.color(QPalette::Highlight), stopped = ui::toneColor(ui::Tone::Warning, pal);
    QColor none = mix(pal.color(QPalette::Window), ink, 0.18), below = mix(pal.color(QPalette::Window), ink, 0.32);
    QColor grid = mix(pal.color(QPalette::Window), ink, 0.12);
    QFont base = font(), bold = font();
    bold.setBold(true);
    QFontMetrics fm(base);
    int w = width();
    auto title = [&](int y, const QString &name, const QString &about) {
        if (!p)
            return;
        p->setFont(bold);
        p->setPen(ink);
        p->drawText(QPoint(0, y + fm.ascent()), name);
        p->setFont(base);
        p->setPen(quiet);
        p->drawText(QPoint(QFontMetrics(bold).horizontalAdvance(name) + 8, y + fm.ascent()), about);
    };
    auto legend = [&](int y, std::initializer_list<std::pair<QColor, QString>> items) {
        int x = 0;
        for (auto &[colour, text] : items)
        {
            if (p)
            {
                p->setPen(Qt::NoPen);
                p->setBrush(colour);
                p->drawRoundedRect(QRectF(x, y + 3, 11, 11), 2.5, 2.5);
                p->setPen(quiet);
                p->drawText(QPoint(x + 16, y + fm.ascent()), text);
            }
            x += 16 + fm.horizontalAdvance(text) + 14;
        }
    };

    // 1. What each kind did.
    int y = 0;
    title(y, tr("What each kind did"), tr("share of its own tries, the most often visible first"));
    y += kTitle;
    legend(y, { { visible, tr("Visibly changed") }, { none, tr("Nothing showed") }, { stopped, tr("Stopped") } });
    y += kLegend;
    int label = 0;
    for (auto &k : st.kinds)
        label = std::max(label, fm.horizontalAdvance(QString::fromLatin1(family_name(k.family))));
    int right = fm.horizontalAdvance(tr("100% visible · 24 kept"));
    int x0 = label + 8, bw = std::max(80, w - x0 - right - 10);
    for (auto &k : st.kinds)
    {
        QString name = QString::fromLatin1(family_name(k.family));
        QRectF row(0, y, w, kRow);
        tips.push_back({ row, tr("%1: %2, %3 visibly changed, %4 showed nothing, %5 stopped; %6 kept")
                                  .arg(name, count(k.runs, "1 try", "%1 tries"))
                                  .arg(k.visible)
                                  .arg(k.unchanged)
                                  .arg(k.stopped)
                                  .arg(k.kept) });
        if (p)
        {
            p->setPen(ink);
            p->drawText(QRect(0, y, label, kRow), Qt::AlignRight | Qt::AlignVCenter, name);
            double x = x0;
            for (auto [n, colour] : { std::pair{ k.visible, visible }, std::pair{ k.unchanged, none },
                                      std::pair{ k.stopped, stopped } })
            {
                if (!n || !k.runs)
                    continue;
                double seg = bw * double(n) / k.runs;
                p->setPen(Qt::NoPen);
                p->setBrush(colour);
                p->drawRoundedRect(QRectF(x, y + 4, std::max(1.0, seg - 2), kRow - 8), 2.5, 2.5);
                x += seg;
            }
            QString text = tr("%1% visible").arg(QLocale().toString(k.visible_share() * 100, 'f', 0));
            if (k.kept)
                text += tr(" · %1 kept").arg(k.kept);
            p->setPen(ink);
            p->drawText(QRect(x0 + bw + 8, y, right + 2, kRow), Qt::AlignLeft | Qt::AlignVCenter, text);
        }
        y += kRow;
    }
    y += 6;
    if (!st.detailed)
    {
        y += kGap;
        if (p)
        {
            p->setPen(quiet);
            p->drawText(QRect(0, y, w, kTitle), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                        tr("Explored before the explorer kept every try: how the scores spread and when the finds turned up "
                           "can't be drawn."));
        }
        return;
    }

    int axis = fm.horizontalAdvance("0000") + 6;
    auto plot_area = [&](int top) { return QRectF(axis, top, w - axis - 4, kPlot); };
    auto gridline = [&](const QRectF &a, double f, const QString &tick) {
        if (!p)
            return;
        double gy = a.bottom() - f * a.height();
        p->setPen(QPen(grid, 1));
        p->drawLine(QPointF(a.left(), gy), QPointF(a.right(), gy));
        p->setPen(quiet);
        p->drawText(QRectF(0, gy - fm.height() / 2.0, axis - 6, fm.height()), Qt::AlignRight | Qt::AlignVCenter, tick);
    };

    // 2. How the scores spread.
    y += kGap;
    title(y, tr("How the scores spread"),
          st.by_find ? tr("tries that visibly changed something; the line is normal play's best")
                     : tr("tries that visibly changed something; from the line up they can be kept"));
    y += kTitle;
    if (p)
    {
        p->setPen(quiet);
        p->drawText(QPoint(0, y + fm.ascent()), tr("%1 ran and changed nothing you could see (score 0); they aren't drawn.")
                                                     .arg(count(st.zero_scores, "1 try", "%1 tries")));
    }
    y += kLegend;
    {
        QRectF a = plot_area(y);
        uint32_t most = 1;
        for (auto &b : st.bins)
            most = std::max(most, b.count);
        for (double f : { 0.0, 0.5, 1.0 })
            gridline(a, f, QLocale().toString(qulonglong(std::lround(f * most))));
        double slot = a.width() / st.bins.size();
        for (size_t i = 0; i < st.bins.size(); i++)
        {
            auto &b = st.bins[i];
            QRectF col(a.left() + i * slot + 3, a.top(), slot - 6, a.height());
            auto two = [](double v) { return QLocale().toString(std::round(v * 100) / 100, 'g', QLocale::FloatingPointShortest); };
            QString range = std::isinf(b.hi) ? tr("%1 and up").arg(two(b.lo)) : tr("%1 to %2").arg(two(b.lo), two(b.hi));
            tips.push_back({ col, tr("Score %1: %2").arg(range, count(b.count, "1 try", "%1 tries")) });
            if (!p)
                continue;
            double bh = a.height() * b.count / most;
            if (b.count)
            {
                p->setPen(Qt::NoPen);
                p->setBrush(b.kept_side ? visible : below);
                p->drawRoundedRect(QRectF(col.left(), a.bottom() - std::max(1.0, bh), col.width(), std::max(1.0, bh)), 2.5,
                                   2.5);
            }
            p->setPen(quiet);
            p->drawText(QRectF(col.left() - 6, a.bottom() + 2, col.width() + 12, fm.height()), Qt::AlignHCenter,
                        std::isinf(b.hi) ? QString("%1+").arg(two(b.lo)) : two(b.lo));
            if (i > 0 && b.kept_side && !st.bins[i - 1].kept_side)
            {
                double lx = a.left() + i * slot;
                p->setPen(QPen(quiet, 1.5, Qt::DashLine));
                p->drawLine(QPointF(lx, a.top()), QPointF(lx, a.bottom()));
                p->drawText(QPointF(lx + 4, a.top() + fm.ascent()), tr("keep line %1").arg(two(st.keep_line)));
            }
        }
        y += kPlot + 30;
    }

    // 3. When the good finds turned up.
    y += kGap;
    title(y, tr("When the good finds turned up"),
          st.by_find ? tr("each find by its strength (1: as different as normal play gets; +1 for each way it acts "
                          "different); the line is the best so far")
                     : tr("each try that scored %1 or more; the line is the best so far").arg(kMinScore));
    y += kTitle;
    legend(y, { { visible, tr("Kept") }, { mix(visible, pal.color(QPalette::Window), 0.55), tr("Good, not in the best 24") } });
    y += kLegend;
    {
        QRectF a = plot_area(y);
        double ymax = 3;
        for (auto &f : st.finds)
            ymax = std::max(ymax, f.value * 1.08);
        double n = std::max<uint32_t>(1, st.tried);
        auto X = [&](double at) { return a.left() + a.width() * at / n; };
        auto Y = [&](double v) { return a.bottom() - a.height() * v / ymax; };
        for (int v = 0; v <= int(ymax); v++)
            gridline(a, v / ymax, QString::number(v));
        if (p)
        {
            p->setPen(QPen(quiet, 1.5, Qt::DashLine));
            double line = st.by_find ? 1.0 : kMinScore;
            p->drawLine(QPointF(a.left(), Y(line)), QPointF(a.right(), Y(line)));
            if (!st.best.empty())
            {
                QPainterPath best;
                best.moveTo(X(st.best[0].first), Y(st.best[0].second));
                for (size_t i = 1; i < st.best.size(); i++)
                {
                    best.lineTo(X(st.best[i].first), Y(st.best[i - 1].second));
                    best.lineTo(X(st.best[i].first), Y(st.best[i].second));
                }
                best.lineTo(X(n), Y(st.best.back().second));
                p->setPen(QPen(mix(ink, pal.color(QPalette::Window), 0.3), 2));
                p->setBrush(Qt::NoBrush);
                p->drawPath(best);
            }
            p->setPen(quiet);
            p->drawText(QRectF(a.left(), a.bottom() + 2, a.width(), fm.height()), Qt::AlignLeft, QStringLiteral("0"));
            p->drawText(QRectF(a.left(), a.bottom() + 2, a.width(), fm.height()), Qt::AlignHCenter,
                        tr("tries, in the order tried"));
            p->drawText(QRectF(a.left(), a.bottom() + 2, a.width(), fm.height()), Qt::AlignRight,
                        QLocale().toString(st.tried));
        }
        for (auto &f : st.finds)
        {
            QPointF c(X(f.at), Y(f.value));
            QString fam = f.at < families.size() ? QString::fromLatin1(family_name(families[f.at])) : QString();
            tips.push_back({ QRectF(c.x() - 6, c.y() - 6, 12, 12),
                             (st.by_find ? tr("Try %1 (%2): find %3%4") : tr("Try %1 (%2): score %3%4"))
                                 .arg(f.at + 1)
                                 .arg(fam)
                                 .arg(QLocale().toString(f.value, 'f', 2))
                                 .arg(f.kept ? tr(", kept") : QString()) });
            if (!p)
                continue;
            p->setPen(QPen(visible, 2));
            p->setBrush(f.kept ? visible : mix(visible, pal.color(QPalette::Window), 0.55)); // as the legend
            p->drawEllipse(c, 4.0, 4.0);
        }
    }
}

void ExploreCharts::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    draw(&p);
}

QString ExploreCharts::tipAt(QPoint at) const
{
    draw(nullptr);
    // The smallest area under the point (a dot over a row).
    const Tip *best = nullptr;
    for (auto &t : tips)
        if (t.area.contains(at) && (!best || t.area.width() * t.area.height() < best->area.width() * best->area.height()))
            best = &t;
    return best ? best->text : QString();
}

bool ExploreCharts::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip)
    {
        auto help = static_cast<QHelpEvent *>(e);
        QString text = tipAt(help->pos());
        if (text.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), text, this);
        return true;
    }
    return QWidget::event(e);
}
