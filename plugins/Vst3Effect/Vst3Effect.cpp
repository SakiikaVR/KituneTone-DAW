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

#include "AudioBusHandle.h"
#include "AudioEngine.h"
#include "Clip.h"
#include "EffectChain.h"
#include "Engine.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"
#include "Vst3AraHost.h"
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




int Vst3Effect::syncAraFromTrack()
{
	if (m_plugin == nullptr || !m_plugin->hasAra()) { return 0; }

	// EffectChain has no parent pointer, so find the sample track whose effect
	// chain contains this effect
	SampleTrack* owner = nullptr;
	auto* song = Engine::getSong();
	if (song == nullptr) { return 0; }
	for (Track* t : song->tracks())
	{
		if (t->type() != Track::Type::Sample) { continue; }
		auto st = dynamic_cast<SampleTrack*>(t);
		if (st != nullptr && st->audioBusHandle()->effects() == effectChain())
		{
			owner = st;
			break;
		}
	}
	araDebugLog(QString("syncAraFromTrack: owner track=%1").arg(owner ? "found" : "NOT FOUND"));
	if (owner == nullptr) { return 0; }

	const auto bpm = song->getTempo();

	std::vector<Vst3Plugin::AraSource> sources;
	araDebugLog(QString("  track has %1 clip(s)").arg(owner->getClips().size()));
	for (Clip* c : owner->getClips())
	{
		auto sc = dynamic_cast<SampleClip*>(c);
		if (sc == nullptr || sc->sampleFile().isEmpty())
		{
			araDebugLog(QString("  clip skipped (sampleClip=%1 file=%2)")
					.arg(sc != nullptr).arg(sc ? sc->sampleFile() : "n/a"));
			continue;
		}

		Vst3Plugin::AraSource src;
		src.file = sc->sampleFile();
		src.startInSongSeconds = sc->startPosition().getTimeInMilliseconds(bpm) / 1000.0;
		src.durationSeconds = sc->length().getTimeInMilliseconds(bpm) / 1000.0;
		src.offsetInSourceSeconds = sc->startTimeOffset().getTimeInMilliseconds(bpm) / 1000.0;
		sources.push_back(src);
	}
	if (sources.empty()) { return 0; }

	Engine::audioEngine()->requestChangeInModel();
	const bool ok = m_plugin->enableAra(sources);
	Engine::audioEngine()->doneChangeInModel();
	return ok ? static_cast<int>(sources.size()) : 0;
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

	// While ARA is active, keep processing every period (even during silence /
	// when the transport is stopped) so the plug-in keeps receiving the current
	// transport state and its playhead follows the DAW's stop as well as play.
	if (m_plugin != nullptr && m_plugin->araActive())
	{
		return ProcessStatus::Continue;
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

	// experimental ARA: automatically expose the track's audio clips to the
	// plug-in (e.g. Melodyne / Vovious) and offer a button to re-sync after the
	// clips on the track change.
	if (plugin != nullptr && plugin->hasAra())
	{
		auto araLabel = new QLabel(this);
		auto araButton = new QPushButton(tr("Sync track audio to ARA"), this);

		auto doSync = [controls, araLabel]
		{
			int n = controls->m_effect->syncAraFromTrack();
			araLabel->setText(n > 0
					? tr("ARA: %1 region(s) from track").arg(n)
					: tr("ARA: no audio clips on this track"));
		};
		connect(araButton, &QPushButton::clicked, this, doSync);
		layout->addWidget(araLabel);
		layout->addWidget(araButton);

		// try once immediately so it "just works" when the track already has clips
		doSync();

		setFixedSize(280, 150);
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
	auto p = plugin();
	if (p == nullptr) { return; }

	// pick up audio clips that were added after the plug-in was loaded:
	// (re-)build the ARA document from the track's current clips before showing
	// the editor, unless ARA is already active
	if (p->hasAra() && !p->araActive())
	{
		static_cast<Vst3EffectControls*>(m_effectControls)->m_effect->syncAraFromTrack();
	}

	p->toggleEditor();
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
