/*
 * Vst3AraHost.h - experimental ARA 2 host integration for LMMS
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

#ifndef LMMS_VST3_ARA_HOST_H
#define LMMS_VST3_ARA_HOST_H

#ifdef LMMS_HAVE_ARA

#include <QString>

#include <memory>
#include <vector>

#include "ARA_API/ARAInterface.h"
#include "ARA_API/ARAVST3.h"
#include "ARA_Library/Dispatch/ARAHostDispatch.h"

namespace lmms
{

//! Host-side audio storage + ARA controller interfaces for a single audio source.
//! This is an experimental, minimal ARA 2 host: it exposes one audio file as an
//! audio source, wraps it in a single playback region, and binds a VST3 plug-in
//! instance as the ARA playback renderer / editor view so that ARA plug-ins such
//! as Melodyne or Vovious can analyse and re-render the material.
class Vst3AraDocument
{
public:
	Vst3AraDocument();
	~Vst3AraDocument();

	//! Build the ARA document model for the given audio file and bind the plug-in.
	//! - araFactory:   the plug-in's ARA factory (from IMainFactory::getFactory())
	//! - entryPoint:   the VST3 IPlugInEntryPoint2 of the audio processor component
	//! - file:         path to the audio file backing the audio source
	//! - startInSongSeconds / durationSeconds: placement of the region on the timeline
	//! Returns true on success.
	bool setup(const ARA::ARAFactory* araFactory,
			ARA::IPlugInEntryPoint2* entryPoint,
			const QString& file, double startInSongSeconds, double durationSeconds);

	bool isValid() const { return m_playbackRenderer != nullptr; }

private:
	void teardown();

	struct Impl;
	std::unique_ptr<Impl> m_impl;

	ARA::Host::DocumentController* m_documentController = nullptr;
	std::unique_ptr<ARA::Host::PlaybackRenderer> m_playbackRenderer;

	ARA::ARAMusicalContextRef m_musicalContext = nullptr;
	ARA::ARARegionSequenceRef m_regionSequence = nullptr;
	ARA::ARAAudioSourceRef m_audioSource = nullptr;
	ARA::ARAAudioModificationRef m_audioModification = nullptr;
	ARA::ARAPlaybackRegionRef m_playbackRegion = nullptr;
};


//! One-time global initialization of an ARA factory (initializeARAWithConfiguration).
//! Safe to call repeatedly; only the first call per factory performs the init.
void araEnsureFactoryInitialized(const ARA::ARAFactory* factory);


} // namespace lmms

#endif // LMMS_HAVE_ARA

#endif // LMMS_VST3_ARA_HOST_H
