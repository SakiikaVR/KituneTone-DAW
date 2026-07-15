/*
 * TriangleSynth.cpp - a minimal real VST3 triangle-wave synth
 *
 * Bundled with KitsuneTone as its default sound source. Built with the vendored
 * Steinberg VST3 SDK (src/3rdparty/vst3sdk) as a standalone .vst3 module.
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version. It is distributed WITHOUT ANY WARRANTY; see the GNU GPL for details.
 */

#include <cmath>

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/vstspeaker.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace kitsune {

// unique class id for the triangle synth
static const FUID kTriangleSynthUID(0x7A11C0E5, 0x11EE4B7A, 0x9C3D5F21, 0x7213A4B6);

//! A tiny polyphonic triangle-wave synth. Reads note on/off events and renders
//! band-unlimited triangle waves with a short attack/release to avoid clicks.
class TriangleSynth : public SingleComponentEffect
{
public:
	static FUnknown* createInstance(void*)
	{
		return static_cast<IAudioProcessor*>(new TriangleSynth());
	}

	tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE
	{
		const tresult result = SingleComponentEffect::initialize(context);
		if (result != kResultOk) { return result; }
		addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
		addEventInput(STR16("Event In"), 16);
		return kResultOk;
	}

	tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
			SpeakerArrangement* outputs, int32 numOuts) SMTG_OVERRIDE
	{
		if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo)
		{
			return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
		}
		return kResultFalse;
	}

	tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE
	{
		return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
	}

	tresult PLUGIN_API setupProcessing(ProcessSetup& setup) SMTG_OVERRIDE
	{
		m_sampleRate = static_cast<float>(setup.sampleRate > 0 ? setup.sampleRate : 44100.0);
		// ~4 ms attack, ~150 ms release
		m_attackInc = 1.0f / (0.004f * m_sampleRate);
		m_releaseInc = 1.0f / (0.15f * m_sampleRate);
		return SingleComponentEffect::setupProcessing(setup);
	}

	tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE
	{
		if (state) { for (auto& v : m_voices) { v = Voice{}; } }
		return kResultOk;
	}

	tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE
	{
		if (data.inputEvents != nullptr)
		{
			const int32 count = data.inputEvents->getEventCount();
			for (int32 i = 0; i < count; ++i)
			{
				Event e;
				if (data.inputEvents->getEvent(i, e) != kResultOk) { continue; }
				if (e.type == Event::kNoteOnEvent) { noteOn(e.noteOn.pitch, e.noteOn.velocity); }
				else if (e.type == Event::kNoteOffEvent) { noteOff(e.noteOff.pitch); }
			}
		}

		if (data.numOutputs < 1 || data.numSamples <= 0) { return kResultOk; }
		AudioBusBuffers& out = data.outputs[0];
		const int32 frames = data.numSamples;
		float* left = out.numChannels > 0 ? out.channelBuffers32[0] : nullptr;
		float* right = out.numChannels > 1 ? out.channelBuffers32[1] : left;

		for (int32 s = 0; s < frames; ++s)
		{
			if (left != nullptr) { left[s] = 0.0f; }
			if (right != nullptr && right != left) { right[s] = 0.0f; }
		}

		for (auto& v : m_voices)
		{
			if (v.state == Voice::Off) { continue; }
			for (int32 s = 0; s < frames; ++s)
			{
				if (v.state == Voice::Attack)
				{
					v.env += m_attackInc;
					if (v.env >= 1.0f) { v.env = 1.0f; v.state = Voice::Sustain; }
				}
				else if (v.state == Voice::Release)
				{
					v.env -= m_releaseInc;
					if (v.env <= 0.0f) { v.env = 0.0f; v.state = Voice::Off; }
				}

				// triangle wave, scaled to keep headroom for stacked voices
				const float tri = 4.0f * std::fabs(v.phase - 0.5f) - 1.0f;
				const float value = tri * v.env * v.velocity * 0.5f;
				if (left != nullptr) { left[s] += value; }
				if (right != nullptr && right != left) { right[s] += value; }

				v.phase += v.inc;
				if (v.phase >= 1.0f) { v.phase -= 1.0f; }
				if (v.state == Voice::Off) { break; }
			}
		}

		out.silenceFlags = 0;
		return kResultOk;
	}

private:
	struct Voice
	{
		enum State { Off, Attack, Sustain, Release } state = Off;
		float phase = 0.0f;
		float inc = 0.0f;
		float velocity = 0.0f;
		float env = 0.0f;
		int16 pitch = -1;
	};

	void noteOn(int16 pitch, float velocity)
	{
		Voice* target = nullptr;
		for (auto& v : m_voices) { if (v.state == Voice::Off) { target = &v; break; } }
		if (target == nullptr) { target = &m_voices[0]; } // steal the first voice
		target->pitch = pitch;
		const float freq = 440.0f * std::pow(2.0f, (pitch - 69) / 12.0f);
		target->inc = freq / m_sampleRate;
		target->velocity = velocity;
		target->env = 0.0f;
		target->phase = 0.0f;
		target->state = Voice::Attack;
	}

	void noteOff(int16 pitch)
	{
		for (auto& v : m_voices)
		{
			if (v.pitch == pitch && v.state != Voice::Off && v.state != Voice::Release)
			{
				v.state = Voice::Release;
			}
		}
	}

	static constexpr int kNumVoices = 16;
	Voice m_voices[kNumVoices];
	float m_sampleRate = 44100.0f;
	float m_attackInc = 0.01f;
	float m_releaseInc = 0.001f;
};

} // namespace kitsune


//------------------------------------------------------------------------
// module entry points required by the SDK's dllmain.cpp
//------------------------------------------------------------------------
bool InitModule() { return true; }
bool DeinitModule() { return true; }

//------------------------------------------------------------------------
// plug-in factory
//------------------------------------------------------------------------
BEGIN_FACTORY_DEF("KitsuneTone",
		"https://github.com/SakiikaVR/KituneTone-DAW",
		"mailto:noreply@kitsunetone.local")

	DEF_CLASS2(INLINE_UID_FROM_FUID(kitsune::kTriangleSynthUID),
			PClassInfo::kManyInstances,
			kVstAudioEffectClass,
			"Triangle Synth",
			0,                       // combined component (not distributable)
			"Instrument|Synth",
			"1.0.0.0",
			kVstVersionString,
			kitsune::TriangleSynth::createInstance)

END_FACTORY
