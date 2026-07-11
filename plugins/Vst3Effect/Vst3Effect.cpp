/*
 * Vst3Effect.cpp - effect plugin hosting native VST3 effects
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

#include "Vst3Effect.h"

#include <QDomDocument>
#include <QDomElement>
#include <QFileInfo>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>

#include "ConfigManager.h"
#include "FileDialog.h"
#include <QMutexLocker>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>
#include <cstring>

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Vst3Plugin.h"
#include "Vst3SubPluginFeatures.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT vst3effect_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"VST3",
	QT_TRANSLATE_NOOP( "PluginBrowser",
			"plugin for using native VST3 effects inside LMMS." ),
	"LMMS Developers",
	0x0100,
	Plugin::Type::Effect,
	new PluginPixmapLoader("logo"),
	nullptr,
	new Vst3SubPluginFeatures( Plugin::Type::Effect )
} ;

}




Vst3Effect::Vst3Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&vst3effect_plugin_descriptor, parent, key),
	m_key(*key),
	m_controls(this)
{
	bool loaded = false;
	if (!m_key.attributes["file"].isEmpty())
	{
		loaded = openPlugin(m_key.attributes["file"], m_key.attributes["uid"]);
	}
	if (loaded)
	{
		setDisplayName(m_plugin->name());
	}
	setDontRun(!loaded);
}




bool Vst3Effect::openPlugin(const QString& file, const QString& uid)
{
	QMutexLocker lock(&m_pluginMutex);

	auto plugin = std::make_unique<Vst3Plugin>(file, Vst3Plugin::Kind::Effect, uid);
	if (plugin->failed())
	{
		collectErrorForUI(Vst3Plugin::tr("The VST3 plugin %1 could not be loaded: %2")
				.arg(file, plugin->errorString()));
		return false;
	}

	m_plugin = std::move(plugin);
	m_key.attributes["file"] = file;
	m_key.attributes["uid"] = m_plugin->uidString();
	return true;
}




Effect::ProcessStatus Vst3Effect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	static thread_local auto tempBuf = std::array<SampleFrame, MAXIMUM_BUFFER_SIZE>();

	std::memcpy(tempBuf.data(), buf, sizeof(SampleFrame) * frames);
	if (m_pluginMutex.tryLock(Engine::getSong()->isExporting() ? -1 : 0))
	{
		if (m_plugin != nullptr) { m_plugin->process(tempBuf.data(), tempBuf.data(), frames); }
		m_pluginMutex.unlock();
	}

	const float w = wetLevel();
	const float d = dryLevel();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		buf[f][0] = w * tempBuf[f][0] + d * buf[f][0];
		buf[f][1] = w * tempBuf[f][1] + d * buf[f][1];
	}

	return ProcessStatus::ContinueIfNotQuiet;
}




Vst3EffectControls::Vst3EffectControls(Vst3Effect* effect) :
	EffectControls(effect),
	m_effect(effect)
{
}




void Vst3EffectControls::saveSettings(QDomDocument& doc, QDomElement& element)
{
	QMutexLocker lock(&m_effect->m_pluginMutex);
	if (m_effect->m_plugin != nullptr)
	{
		m_effect->m_plugin->saveState(doc, element);
	}
}




void Vst3EffectControls::loadSettings(const QDomElement& element)
{
	QMutexLocker lock(&m_effect->m_pluginMutex);
	if (m_effect->m_plugin != nullptr)
	{
		m_effect->m_plugin->loadState(element);
	}
}




gui::EffectControlDialog* Vst3EffectControls::createView()
{
	return new gui::Vst3EffectControlDialog(this);
}




namespace gui
{


Vst3EffectControlDialog::Vst3EffectControlDialog(Vst3EffectControls* controls) :
	EffectControlDialog(controls)
{
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);

	auto plugin = controls->m_effect->plugin();

	QString text = plugin != nullptr
			? plugin->name() + " (" + plugin->vendor() + ")"
			: tr("No plugin loaded");
	if (plugin != nullptr && plugin->hasAra())
	{
		text += "\n" + tr("ARA: %1 (experimental)").arg(plugin->araName());
	}
	auto nameLabel = new QLabel(text, this);
	layout->addWidget(nameLabel);

	auto guiButton = new QPushButton(tr("Show/hide GUI"), this);
	guiButton->setEnabled(plugin != nullptr);
	connect(guiButton, &QPushButton::clicked, this, [controls]
	{
		auto p = controls->m_effect->plugin();
		if (p != nullptr) { p->toggleEditor(); }
	});
	layout->addWidget(guiButton);

	// experimental ARA: let the user pick the audio file this effect should
	// analyse/re-render (e.g. for Melodyne / Vovious), then bind it as an ARA
	// playback renderer placed at the start of the song.
	if (plugin != nullptr && plugin->hasAra())
	{
		auto araButton = new QPushButton(tr("Enable ARA (choose audio)..."), this);
		connect(araButton, &QPushButton::clicked, this, [this, controls, araButton]
		{
			auto p = controls->m_effect->plugin();
			if (p == nullptr) { return; }

			FileDialog ofd(nullptr, tr("Choose audio file for ARA"));
			ofd.setFileMode(FileDialog::ExistingFile);
			ofd.setNameFilters({tr("Audio files (*.wav *.flac *.ogg *.mp3 *.aiff)")});
			ofd.setDirectory(ConfigManager::inst()->userSamplesDir());
			if (ofd.exec() != QDialog::Accepted || ofd.selectedFiles().isEmpty()) { return; }

			const bool ok = p->enableAra(ofd.selectedFiles()[0], 0.0, 0.0);
			araButton->setText(ok ? tr("ARA active: %1").arg(p->araName())
					: tr("ARA setup failed"));
			araButton->setEnabled(!ok);
		});
		layout->addWidget(araButton);
		setFixedSize(260, 140);
	}
	else
	{
		setFixedSize(240, 100);
	}
}




Vst3Plugin* Vst3EffectControlDialog::plugin() const
{
	return static_cast<Vst3EffectControls*>(m_effectControls)->m_effect->plugin();
}




bool Vst3EffectControlDialog::togglesExternalUi() const
{
	auto p = plugin();
	return p != nullptr && p->hasEditor();
}




void Vst3EffectControlDialog::toggleExternalUi()
{
	if (auto p = plugin()) { p->toggleEditor(); }
}


} // namespace gui


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new Vst3Effect(parent,
			static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}


} // namespace lmms
