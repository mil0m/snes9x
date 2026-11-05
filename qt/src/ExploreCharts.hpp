#pragma once
#include <QWidget>

class QPainter;

#include "corrupt/exploration.h"

#include <vector>

// What an exploration did (#80), in three charts drawn from the palette: what each kind of
// corruption did, how the visible changes' scores spread around the keep line, and when the
// good finds turned up. Every bar and dot has a tooltip with its numbers. The same charts as
// the gallery page's (corrupt/exploration.cpp), from the same stats_of.
class ExploreCharts : public QWidget
{
  public:
    explicit ExploreCharts(QWidget *parent = nullptr);
    void setExploration(const corrupt::Exploration &e); // works the stats out and repaints
    const corrupt::ExploreStats &stats() const { return st; }
    QSize sizeHint() const override;
    int heightForWidth(int w) const override;
    bool hasHeightForWidth() const override { return true; }
    // What the tooltip says at a point (empty: nothing there), for the tests.
    QString tipAt(QPoint p) const;

  protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;

  private:
    int kindsHeight() const;
    void draw(QPainter *p) const; // lays the charts out (the tips), and paints them when p is set
    struct Tip
    {
        QRectF area;
        QString text;
    };
    corrupt::ExploreStats st;
    std::vector<corrupt::Family> families; // each try's, for the finds' tooltips
    mutable std::vector<Tip> tips;         // laid out while painting
};
