// Copyright (c) 2018-2019 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/blockneticonlabel.h>

#include <qt/blocknetguiutil.h>

#include <QApplication>
#include <QStyle>
#include <QVariant>

BlocknetIconLabel::BlocknetIconLabel(QPushButton *parent) : QPushButton(parent),
    icon(new QLabel), label(new QLabel), layout(new QHBoxLayout)
{
    this->setMinimumHeight(BGU::spi(40));
    this->setCheckable(true);
    this->setFlat(true);
    this->setCursor(Qt::PointingHandCursor);

    icon->setFixedSize(BGU::spi(30), BGU::spi(30));

    layout->setContentsMargins(QMargins());
    layout->setSpacing(BGU::spi(10));
    layout->addWidget(icon, 0, Qt::AlignVCenter | Qt::AlignLeft);
    layout->addWidget(label, 1, Qt::AlignVCenter | Qt::AlignLeft);

    this->setLayout(layout);

    connect(this, SIGNAL(toggled(bool)), this, SLOT(onSelected(bool)));

    onSelected(false);
}

void BlocknetIconLabel::setIcon(const QString active, const QString disabled) {
    iconActive = active;
    iconDisabled = disabled;
    this->update();
}

void BlocknetIconLabel::setLabel(const QString &label) {
    labelText = label;
    if (this->label->text() != labelText)
        this->label->setText(labelText);
    this->update();
}

void BlocknetIconLabel::paintEvent(QPaintEvent *e) {
    // Only draw the icon if it hasn't already been drawn
    if (iconActiveState == nullptr || *iconActiveState != this->isChecked()) {
        iconActiveState = new bool(this->isChecked());

        QPixmap pm;
        if (this->isChecked())
            pm = QPixmap(iconActive);
        else
            pm = QPixmap(iconDisabled);

        pm.setDevicePixelRatio(BGU::dpr());
        icon->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        icon->setPixmap(pm.scaledToHeight(pm.height()*pm.devicePixelRatio(), Qt::SmoothTransformation));
    }

    QPushButton::paintEvent(e);
}

void BlocknetIconLabel::onSelected(bool selected) {
    label->setProperty("selected", selected);
    label->style()->unpolish(label);
    label->style()->polish(label);
}

BlocknetIconLabel::~BlocknetIconLabel() = default;
