/*
 * Vst3PluginView.h - shared tabbed view for hosted VST3 plug-ins
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_VST3_PLUGIN_VIEW_H
#define LMMS_VST3_PLUGIN_VIEW_H

#include <functional>

#include <QPointer>
#include <QString>
#include <QWidget>

#include "vst3base_export.h"

class QTabWidget;
class QVBoxLayout;

namespace lmms
{

class Vst3Plugin;

namespace gui
{

//! Common GUI / Parameters tab set used by VST3 effects and instruments.
class VST3BASE_EXPORT Vst3PluginView : public QWidget
{
public:
	explicit Vst3PluginView(Vst3Plugin* plugin, QWidget* parent = nullptr);

	QTabWidget* tabs() const { return m_tabs; }
	QVBoxLayout* guiLayout() const { return m_guiLayout; }

	//! Add a tab which cannot force the plug-in window larger than its GUI.
	QWidget* addTab(QWidget* content, const QString& label, bool scrollable = true);

	//! Called immediately before the native editor is created (used by ARA).
	void setBeforeEditorCallback(std::function<void()> callback);
	//! Called after editor creation and whenever the plug-in requests a resize.
	void setContentsChangedCallback(std::function<void()> callback);

	//! Lazily create and embed the native editor in the GUI tab.
	void ensureEmbeddedEditor();

private:
	QPointer<Vst3Plugin> m_plugin;
	QTabWidget* m_tabs = nullptr;
	QWidget* m_guiTab = nullptr;
	QVBoxLayout* m_guiLayout = nullptr;
	bool m_editorTried = false;
	std::function<void()> m_beforeEditor;
	std::function<void()> m_contentsChanged;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_VST3_PLUGIN_VIEW_H
