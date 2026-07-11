/*
 * Vst3SubPluginFeatures.cpp - derivation from
 *                             Plugin::Descriptor::SubPluginFeatures for
 *                             hosting native VST3 plug-ins
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "Vst3SubPluginFeatures.h"

#include <QFileInfo>
#include <QLabel>

#include "Vst3Plugin.h"

namespace lmms
{


Vst3SubPluginFeatures::Vst3SubPluginFeatures(Plugin::Type type) :
	SubPluginFeatures(type)
{
}




void Vst3SubPluginFeatures::fillDescriptionWidget(QWidget* parent, const Key* key) const
{
	new QLabel(QWidget::tr("Name: ") + key->name, parent);
	new QLabel(QWidget::tr("File: ") + key->attributes["file"], parent);
}




void Vst3SubPluginFeatures::listSubPluginKeys(const Plugin::Descriptor* desc, KeyList& kl) const
{
	for (const auto& path : Vst3Plugin::modulePaths())
	{
		const QString file = QString::fromStdString(path);
		Key::AttributeMap am;
		am["file"] = file;
		kl.push_back(Key(desc, QFileInfo(file).completeBaseName(), am));
	}
}


} // namespace lmms
