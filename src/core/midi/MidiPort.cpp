/*
 * MidiPort.cpp - abstraction of MIDI-ports which are part of LMMS's MIDI-
 *                sequencing system
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include <algorithm>

#include <QDomElement>
#include <QMutex>
#include <QVector>

#include "MidiPort.h"
#include "MidiClient.h"
#include "MidiDummy.h"
#include "MidiEventProcessor.h"
#include "Note.h"
#include "Song.h"
#include "MidiController.h"


namespace lmms
{


static MidiDummy s_dummyClient;

// registry of all ports for the internal (track-to-track) MIDI bus
static QVector<MidiPort*> s_ports;
static QMutex s_portsMutex;



MidiPort::MidiPort( const QString& name,
					MidiClient* client,
					MidiEventProcessor* eventProcessor,
					Model* parent,
					Mode mode ) :
	Model( parent ),
	m_readablePortsMenu( nullptr ),
	m_writablePortsMenu( nullptr ),
	m_midiClient( client ),
	m_midiEventProcessor( eventProcessor ),
	m_mode( mode ),
	m_inputChannelModel( 0, 0, MidiChannelCount, this, tr( "Input channel" ) ),
	m_outputChannelModel( 1, 0, MidiChannelCount, this, tr( "Output channel" ) ),
	m_inputControllerModel(MidiController::NONE, MidiController::NONE, MidiControllerCount - 1, this, tr( "Input controller" )),
	m_outputControllerModel(MidiController::NONE, MidiController::NONE, MidiControllerCount - 1, this, tr( "Output controller" )),
	m_fixedInputVelocityModel( -1, -1, MidiMaxVelocity, this, tr( "Fixed input velocity" ) ),
	m_fixedOutputVelocityModel( -1, -1, MidiMaxVelocity, this, tr( "Fixed output velocity" ) ),
	m_fixedOutputNoteModel( -1, -1, MidiMaxKey, this, tr( "Fixed output note" ) ),
	m_outputProgramModel( 1, 1, MidiProgramCount, this, tr( "Output MIDI program" ) ),
	m_baseVelocityModel( MidiMaxVelocity/2, 1, MidiMaxVelocity, this, tr( "Base velocity" ) ),
	m_readableModel( false, this, tr( "Receive MIDI-events" ) ),
	m_writableModel( false, this, tr( "Send MIDI-events" ) )
{
	m_midiClient->addPort( this );

	{
		QMutexLocker lock(&s_portsMutex);
		s_ports.append(this);
	}

	m_readableModel.setValue( m_mode == Mode::Input || m_mode == Mode::Duplex );
	m_writableModel.setValue( m_mode == Mode::Output || m_mode == Mode::Duplex );

	connect( &m_readableModel, SIGNAL(dataChanged()),
			this, SLOT(updateMidiPortMode()), Qt::DirectConnection );
	connect( &m_writableModel, SIGNAL(dataChanged()),
			this, SLOT(updateMidiPortMode()), Qt::DirectConnection );
	connect( &m_outputProgramModel, SIGNAL(dataChanged()),
			this, SLOT(updateOutputProgram()), Qt::DirectConnection );


	// when using with non-raw-clients we can provide buttons showing
	// our port-menus when being clicked
	if( m_midiClient->isRaw() == false )
	{
		updateReadablePorts();
		updateWritablePorts();

		// we want to get informed about port-changes!
		m_midiClient->connectRPChanged( this, SLOT(updateReadablePorts()));
		m_midiClient->connectWPChanged( this, SLOT(updateWritablePorts()));
	}

	updateMidiPortMode();
}




MidiPort::~MidiPort()
{
	{
		QMutexLocker lock(&s_portsMutex);
		s_ports.removeAll(this);
	}

	// unsubscribe ports
	m_readableModel.setValue( false );
	m_writableModel.setValue( false );

	// and finally unregister ourself
	m_midiClient->removePort( this );
}




void MidiPort::setName( const QString& name )
{
	setDisplayName( name );
	m_midiClient->applyPortName( this );
}




void MidiPort::setMode( Mode mode )
{
	m_mode = mode;
	m_midiClient->applyPortMode( this );
}




void MidiPort::processInEvent( const MidiEvent& event, const TimePos& time )
{
	// mask event
	if( isInputEnabled() &&
		( inputChannel() == 0 || inputChannel()-1 == event.channel() ) )
	{
		MidiEvent inEvent = event;
		if( event.type() == MidiNoteOn ||
			event.type() == MidiNoteOff ||
			event.type() == MidiKeyPressure )
		{
			if( inEvent.key() < 0 || inEvent.key() >= NumKeys )
			{
				return;
			}

			if( fixedInputVelocity() >= 0 && inEvent.velocity() > 0 )
			{
				inEvent.setVelocity( fixedInputVelocity() );
			}
		}

		if (m_midiEventProcessor) { m_midiEventProcessor->processInEvent(inEvent, time); }
	}
}




void MidiPort::processOutEvent( const MidiEvent& event, const TimePos& time )
{
	// When output is enabled, route midi events if the selected channel matches
	// the event channel or if there's no selected channel (value 0, represented by "--")
	if( isOutputEnabled() && ( outputChannel() == 0 || realOutputChannel() == event.channel() ) )
	{
		MidiEvent outEvent = event;

		if( fixedOutputVelocity() >= 0 && event.velocity() > 0 &&
			( event.type() == MidiNoteOn || event.type() == MidiKeyPressure ) )
		{
			outEvent.setVelocity( fixedOutputVelocity() );
		}

		if( fixedOutputNote() >= 0 &&
			( event.type() == MidiNoteOn || event.type() == MidiNoteOff || event.type() == MidiKeyPressure ) )
		{
			outEvent.setKey( fixedOutputNote() );
		}

		m_midiClient->processOutEvent( outEvent, time, this );

		// Internal MIDI bus: tracks with enabled MIDI input receive what
		// other tracks output, like a virtual MIDI cable - no loopback
		// driver needed. Events are delivered through the receiving port's
		// event loop: processInEvent() (note-off model changes etc.) is not
		// safe to call synchronously from another track's render thread.
		// Keep the registry locked until each queued call has been attached to
		// its QObject context.  Copying the raw pointers and unlocking here left
		// a race where a track could destroy its MidiPort between the snapshot
		// and invokeMethod(), causing a use-after-free during playback/shutdown.
		// An event delivered by this bus may be replayed much later by a note
		// handle or a VST3 MIDI generator, after a thread-local recursion guard
		// has disappeared. Preserve provenance as a hop count on the event
		// itself: multi-track chains keep working up to MaxInternalBusHops
		// while feedback loops still decay after a few round trips.
		if (outEvent.internalBusHops() < MaxInternalBusHops)
		{
			MidiEvent deliveryEvent = outEvent;
			deliveryEvent.setSource(MidiEvent::Source::Internal);
			deliveryEvent.setSourcePort(nullptr);
			deliveryEvent.setInternalBusHops(outEvent.internalBusHops() + 1);
			QMutexLocker lock(&s_portsMutex);
			for (MidiPort* port : s_ports)
			{
				// Skip ports that would discard the event at drain time anyway;
				// this runs per event on the rendering path, so don't lock and
				// allocate for the (typical) input-disabled ports.
				if (port == this || !port->isInputEnabled()) { continue; }
				port->queueInternalEvent(deliveryEvent, time);
			}
		}
	}
}




bool MidiPort::isReleaseEvent(const MidiEvent& event)
{
	return event.type() == MidiNoteOff
		|| (event.type() == MidiNoteOn && event.velocity() == 0)
		|| (event.type() == MidiControlChange
			&& (event.controllerNumber() == MidiControllerAllSoundOff
				|| event.controllerNumber() == MidiControllerAllNotesOff
				|| (event.controllerNumber() == MidiControllerSustain
					&& event.controllerValue() <= MidiMaxControllerValue / 2)));
}




void MidiPort::queueInternalEvent(const MidiEvent& event, const TimePos& time)
{
	bool scheduleDrain = false;
	{
		QMutexLocker lock(&m_internalQueueMutex);
		if (m_internalQueue.size() >= MaxInternalQueuedEvents)
		{
			if (!isReleaseEvent(event)) { return; }
			auto replace = std::find_if(m_internalQueue.begin(), m_internalQueue.end(),
				[](const QueuedInputEvent& queued) { return !isReleaseEvent(queued.event); });
			if (replace == m_internalQueue.end()) { return; }
			*replace = QueuedInputEvent{event, time};
		}
		else
		{
			m_internalQueue.push_back(QueuedInputEvent{event, time});
		}

		if (!m_internalDrainScheduled)
		{
			m_internalDrainScheduled = true;
			scheduleDrain = true;
		}
	}

	if (scheduleDrain && !QMetaObject::invokeMethod(this,
			[this] { drainInternalEvents(); }, Qt::QueuedConnection))
	{
		QMutexLocker lock(&m_internalQueueMutex);
		m_internalDrainScheduled = false;
		m_internalQueue.clear();
	}
}




void MidiPort::drainInternalEvents()
{
	std::vector<QueuedInputEvent> batch;
	bool hasMore = false;
	{
		QMutexLocker lock(&m_internalQueueMutex);
		const std::size_t count = std::min(InternalDrainBatchSize, m_internalQueue.size());
		batch.reserve(count);
		for (std::size_t i = 0; i < count; ++i)
		{
			batch.push_back(std::move(m_internalQueue.front()));
			m_internalQueue.pop_front();
		}
		hasMore = !m_internalQueue.empty();
		if (!hasMore) { m_internalDrainScheduled = false; }
	}

	for (const auto& queued : batch) { processInEvent(queued.event, queued.time); }

	if (hasMore && !QMetaObject::invokeMethod(this,
			[this] { drainInternalEvents(); }, Qt::QueuedConnection))
	{
		QMutexLocker lock(&m_internalQueueMutex);
		m_internalDrainScheduled = false;
		m_internalQueue.clear();
	}
}




void MidiPort::saveSettings( QDomDocument& doc, QDomElement& thisElement )
{
	m_inputChannelModel.saveSettings( doc, thisElement, "inputchannel" );
	m_outputChannelModel.saveSettings( doc, thisElement, "outputchannel" );
	m_inputControllerModel.saveSettings( doc, thisElement, "inputcontroller" );
	m_outputControllerModel.saveSettings( doc, thisElement, "outputcontroller" );
	m_fixedInputVelocityModel.saveSettings( doc, thisElement, "fixedinputvelocity" );
	m_fixedOutputVelocityModel.saveSettings( doc, thisElement, "fixedoutputvelocity" );
	m_fixedOutputNoteModel.saveSettings( doc, thisElement, "fixedoutputnote" );
	m_outputProgramModel.saveSettings( doc, thisElement, "outputprogram" );
	m_baseVelocityModel.saveSettings( doc, thisElement, "basevelocity" );
	m_readableModel.saveSettings( doc, thisElement, "readable" );
	m_writableModel.saveSettings( doc, thisElement, "writable" );

	if( isInputEnabled() )
	{
		QString rp;
		for( Map::ConstIterator it = m_readablePorts.begin(); it != m_readablePorts.end(); ++it )
		{
			if( it.value() )
			{
				rp += it.key() + ",";
			}
		}
		// cut off comma
		if( rp.length() > 0 )
		{
			rp.truncate( rp.length() - 1 );
		}
		thisElement.setAttribute( "inports", rp );
	}

	if( isOutputEnabled() )
	{
		QString wp;
		for( Map::ConstIterator it = m_writablePorts.begin(); it != m_writablePorts.end(); ++it )
		{
			if( it.value() )
			{
				wp += it.key() + ",";
			}
		}
		// cut off comma
		if( wp.length() > 0 )
		{
			wp.truncate( wp.length() - 1 );
		}
		thisElement.setAttribute( "outports", wp );
	}
}




void MidiPort::loadSettings( const QDomElement& thisElement )
{
	m_inputChannelModel.loadSettings( thisElement, "inputchannel" );
	m_outputChannelModel.loadSettings( thisElement, "outputchannel" );
	m_inputControllerModel.loadSettings( thisElement, "inputcontroller" );
	m_outputControllerModel.loadSettings( thisElement, "outputcontroller" );
	m_fixedInputVelocityModel.loadSettings( thisElement, "fixedinputvelocity" );
	m_fixedOutputVelocityModel.loadSettings( thisElement, "fixedoutputvelocity" );
	m_fixedOutputNoteModel.loadSettings( thisElement, "fixedoutputnote" );
	m_outputProgramModel.loadSettings( thisElement, "outputprogram" );
	m_baseVelocityModel.loadSettings( thisElement, "basevelocity" );
	m_readableModel.loadSettings( thisElement, "readable" );
	m_writableModel.loadSettings( thisElement, "writable" );

	// restore connections

	if( isInputEnabled() )
	{
		QStringList rp = thisElement.attribute( "inports" ).split( ',' );
		for( Map::ConstIterator it = m_readablePorts.begin(); it != m_readablePorts.end(); ++it )
		{
			if( it.value() != ( rp.indexOf( it.key() ) != -1 ) )
			{
				subscribeReadablePort( it.key() );
			}
		}
		emit readablePortsChanged();
	}

	if( isOutputEnabled() )
	{
		QStringList wp = thisElement.attribute( "outports" ).split( ',' );
		for( Map::ConstIterator it = m_writablePorts.begin(); it != m_writablePorts.end(); ++it )
		{
			if( it.value() != ( wp.indexOf( it.key() ) != -1 ) )
			{
				subscribeWritablePort( it.key() );
			}
		}
		emit writablePortsChanged();
	}

	if( thisElement.hasAttribute( "basevelocity" ) == false )
	{
		// for projects created by LMMS < 0.9.92 there's no value for the base
		// velocity and for compat reasons we have to stick with maximum velocity
		// which did not allow note volumes > 100%
		m_baseVelocityModel.setValue( MidiMaxVelocity );
	}
}





void MidiPort::subscribeReadablePort( const QString& port, bool subscribe )
{
	m_readablePorts[port] = subscribe;

	// make sure, MIDI-port is configured for input
	if( subscribe == true && !isInputEnabled() )
	{
		m_readableModel.setValue( true );
	}

	m_midiClient->subscribeReadablePort( this, port, subscribe );
}




void MidiPort::subscribeWritablePort( const QString& port, bool subscribe )
{
	m_writablePorts[port] = subscribe;

	// make sure, MIDI-port is configured for output
	if( subscribe == true && !isOutputEnabled() )
	{
		m_writableModel.setValue( true );
	}
	m_midiClient->subscribeWritablePort( this, port, subscribe );
}




void MidiPort::updateMidiPortMode()
{
	// this small lookup-table makes everything easier
	static const Mode modeTable[2][2] =
	{
		{ Mode::Disabled, Mode::Output },
		{ Mode::Input, Mode::Duplex }
	} ;
	setMode( modeTable[m_readableModel.value()][m_writableModel.value()] );

	// check whether we have to dis-check items in connection-menu
	if( !isInputEnabled() )
	{
		for( Map::ConstIterator it = m_readablePorts.begin(); it != m_readablePorts.end(); ++it )
		{
			// subscribed?
			if( it.value() )
			{
				subscribeReadablePort( it.key(), false );
			}
		}
	}

	if( !isOutputEnabled() )
	{
		for( Map::ConstIterator it = m_writablePorts.begin(); it != m_writablePorts.end(); ++it )
		{
			// subscribed?
			if( it.value() )
			{
				subscribeWritablePort( it.key(), false );
			}
		}
	}

	emit readablePortsChanged();
	emit writablePortsChanged();
	emit modeChanged();

	if( Engine::getSong() )
	{
		Engine::getSong()->setModified();
	}
}




void MidiPort::updateReadablePorts()
{
	// first save all selected ports
	QStringList selectedPorts;
	for( Map::ConstIterator it = m_readablePorts.begin(); it != m_readablePorts.end(); ++it )
	{
		if( it.value() )
		{
			selectedPorts.push_back( it.key() );
		}
	}

	m_readablePorts.clear();
	const QStringList& wp = m_midiClient->readablePorts();
	// now insert new ports and restore selections
	for (const auto& port : wp)
	{
		m_readablePorts[port] = (selectedPorts.indexOf(port) != -1);
	}

	emit readablePortsChanged();
}




void MidiPort::updateWritablePorts()
{
	// first save all selected ports
	QStringList selectedPorts;
	for( Map::ConstIterator it = m_writablePorts.begin(); it != m_writablePorts.end(); ++it )
	{
		if( it.value() )
		{
			selectedPorts.push_back( it.key() );
		}
	}

	m_writablePorts.clear();
	const QStringList & wp = m_midiClient->writablePorts();
	// now insert new ports and restore selections
	for (const auto& port : wp)
	{
		m_writablePorts[port] = (selectedPorts.indexOf(port) != -1);
	}

	emit writablePortsChanged();
}




void MidiPort::updateOutputProgram()
{
	processOutEvent( MidiEvent( MidiProgramChange, realOutputChannel(), outputProgram()-1 ) );
}




void MidiPort::invalidateCilent()
{
	m_midiClient = &s_dummyClient;
}


} // namespace lmms
