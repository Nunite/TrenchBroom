/*
 *  Copyright (C) 2010 Kristian Duske
 *
 *  This file is part of TrenchBroom.
 *
 *  TrenchBroom is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  TrenchBroom is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ui/MiscPreferencePane.h"

#include "PreferenceManager.h"
#include "Preferences.h"

#include <QBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QObject>

namespace tb::ui {

MiscPreferencePane::MiscPreferencePane(QWidget* parent)
	: PreferencePane(parent)
{
	auto* layout = new QVBoxLayout{};
	layout->setContentsMargins(20, 20, 20, 20);
	layout->setSpacing(6);
	layout->addWidget(new QLabel{"Misc preferences"});

		auto* prefixWorldspawnOnCopy = new QCheckBox{"Prefix worldspawn header on copy"};
	layout->addWidget(prefixWorldspawnOnCopy);
 	
	layout->addStretch();
	setLayout(layout);

	// 初始化和绑定事件
	//auto& prefs = PreferenceManager::instance();
	prefixWorldspawnOnCopy->setChecked(pref(Preferences::PrefixWorldspawnHeaderOnCopy));
	QObject::connect(prefixWorldspawnOnCopy, &QCheckBox::toggled, this, [&](bool checked) {
		setPref(Preferences::PrefixWorldspawnHeaderOnCopy, checked);
	});
}

bool MiscPreferencePane::canResetToDefaults() { return true; }
void MiscPreferencePane::doResetToDefaults() {
	resetPref(Preferences::PrefixWorldspawnHeaderOnCopy);
}
void MiscPreferencePane::updateControls() {}
bool MiscPreferencePane::validate() { return true; }

} // namespace tb::ui 