/*
 * PluginBrowser.h - include file for PluginBrowser
 *
 * Copyright (c) 2005-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_GUI_PLUGIN_BROWSER_H
#define LMMS_GUI_PLUGIN_BROWSER_H

#include <QPixmap>

#include "SideBarWidget.h"
#include "Plugin.h"

class QTreeWidget;
class QTreeWidgetItem;

namespace lmms::gui
{

class PluginBrowser : public SideBarWidget
{
	Q_OBJECT
public:
	PluginBrowser( QWidget * _parent );
	~PluginBrowser() override = default;

private slots:
	void onFilterChanged( const QString & filter );

private:
	void addPlugins();
	void updateRootVisibility( int index );
	void updateRootVisibilities();

	//! scan the VST3 / VST2 plug-in folders and list the installed plug-ins
	//! directly under the given root as draggable entries
	void addVstPlugins(QTreeWidgetItem* root, const QStringList& dirs,
			const QString& extension, bool recurseIntoDirs);

	QWidget * m_view;
	QTreeWidget * m_descTree;
};


//! A draggable entry for an installed VST plug-in file (a VST3 bundle or a VST2
//! dll). Dragging it onto a track creates an instrument loaded with the plug-in.
class VstFileWidget : public QWidget
{
public:
	VstFileWidget( const QString & name, const QString & path, QWidget * parent );
	QString name() const { return m_name; }

protected:
	void mousePressEvent( QMouseEvent * me ) override;
	void paintEvent( QPaintEvent * pe ) override;

private:
	constexpr static int DEFAULT_HEIGHT{24};
	QString m_name;
	QString m_path;
};


class PluginDescWidget : public QWidget
{
	Q_OBJECT
public:
	using PluginKey = Plugin::Descriptor::SubPluginFeatures::Key;
	PluginDescWidget( const PluginKey & _pk, QWidget * _parent );
	QString name() const;
	void openInNewInstrumentTrack(QString value);

protected:
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	void enterEvent(QEnterEvent* event) override;
#else
	void enterEvent(QEvent* event) override;
#endif
	void leaveEvent( QEvent * _e ) override;
	void mousePressEvent( QMouseEvent * _me ) override;
	void paintEvent( QPaintEvent * _pe ) override;
	void contextMenuEvent(QContextMenuEvent* e) override;

private:
	constexpr static int DEFAULT_HEIGHT{24};

	PluginKey m_pluginKey;
	QPixmap m_logo;

	bool m_mouseOver;
};


} // namespace lmms::gui

#endif // LMMS_GUI_PLUGIN_BROWSER_H
