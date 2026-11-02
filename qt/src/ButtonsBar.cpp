#include "ButtonsBar.hpp"

#include <QGridLayout>
#include <QPushButton>

namespace
{
bool same(const corrupt::ButtonSlot &a, const corrupt::ButtonSlot &b)
{
    return a.key == b.key && a.label == b.label && a.details == b.details && a.toggle == b.toggle && a.on == b.on;
}
} // namespace

ButtonsBar::ButtonsBar(QWidget *parent) : QWidget(parent)
{
    setObjectName("buttons");
    grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    setVisible(false);
}

void ButtonsBar::setButtons(const std::vector<corrupt::ButtonSlot> &buttons)
{
    bool unchanged = buttons.size() == shown.size() &&
                     std::equal(buttons.begin(), buttons.end(), shown.begin(), same);
    if (unchanged)
        return;
    bool same_set = buttons.size() == shown.size() &&
                    std::equal(buttons.begin(), buttons.end(), shown.begin(),
                               [](auto &a, auto &b) { return a.key == b.key && a.label == b.label && a.toggle == b.toggle; });
    shown = buttons;
    if (same_set)
    {
        // Only on/off moved: don't rebuild under the mouse.
        for (size_t i = 0; i < pushes.size(); i++)
            pushes[i]->setChecked(shown[i].on);
        return;
    }
    for (auto p : pushes)
        delete p;
    pushes.clear();
    for (size_t i = 0; i < shown.size(); i++)
    {
        auto &b = shown[i];
        auto push = new QPushButton(QString::fromStdString(b.label), this);
        push->setObjectName(QString("button_%1").arg(b.key));
        push->setAutoDefault(false);
        push->setCheckable(b.toggle);
        push->setChecked(b.on);
        QString details = QString::fromStdString(b.details);
        push->setToolTip(b.toggle ? tr("%1\nPress to turn it %2").arg(details, b.on ? tr("back") : tr("on"))
                                  : tr("%1\nEach press fires it").arg(details));
        push->setAccessibleName(QString::fromStdString(b.label));
        int key = b.key;
        connect(push, &QPushButton::clicked, this, [this, push, key, toggle = b.toggle, on = b.on] {
            // The ROM decides what's on: show the old state until the next refresh says.
            if (toggle)
                push->setChecked(on);
            if (onPress)
                onPress(key);
        });
        grid->addWidget(push, int(i / 2), int(i % 2));
        pushes.push_back(push);
    }
    setVisible(!shown.empty());
}
