/*
 * Vst3PluginView.cpp - shared tabbed view for hosted VST3 plug-ins
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "Vst3PluginView.h"

#include <QLabel>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

#include <utility>

#include "Vst3Plugin.h"

namespace lmms::gui
{

Vst3PluginView::Vst3PluginView(Vst3Plugin* plugin, QWidget* parent) :
	QWidget(parent),
	m_plugin(plugin)
{
	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);

	m_tabs = new QTabWidget(this);
	outer->addWidget(m_tabs);

	m_guiTab = new QWidget(m_tabs);
	m_guiLayout = new QVBoxLayout(m_guiTab);
	m_guiLayout->setContentsMargins(4, 4, 4, 4);
	m_guiLayout->setSpacing(4);
	m_tabs->addTab(m_guiTab, QStringLiteral("プラグイン画面"));

	QWidget* parameters = m_plugin != nullptr
			? m_plugin->createParameterWidget(m_tabs)
			: nullptr;
	if (parameters == nullptr)
	{
		auto* label = new QLabel(
				QStringLiteral("このプラグインには自動化できるパラメーターがありません。"), m_tabs);
		label->setContentsMargins(8, 8, 8, 8);
		label->setWordWrap(true);
		parameters = label;
	}
	parameters->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	m_tabs->addTab(parameters, QStringLiteral("パラメーター"));
}




QWidget* Vst3PluginView::addTab(QWidget* content, const QString& label, bool scrollable)
{
	if (content == nullptr) { return nullptr; }

	QWidget* tab = content;
	if (scrollable)
	{
		auto* scroll = new QScrollArea(m_tabs);
		scroll->setWidgetResizable(true);
		scroll->setWidget(content);
		tab = scroll;
	}
	tab->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	m_tabs->addTab(tab, label);
	return tab;
}




void Vst3PluginView::setBeforeEditorCallback(std::function<void()> callback)
{
	m_beforeEditor = std::move(callback);
}




void Vst3PluginView::setContentsChangedCallback(std::function<void()> callback)
{
	m_contentsChanged = std::move(callback);
}




void Vst3PluginView::ensureEmbeddedEditor()
{
	if (m_editorTried) { return; }
	m_editorTried = true;

	if (m_plugin == nullptr)
	{
		m_guiLayout->insertWidget(0,
				new QLabel(QStringLiteral("VST3プラグインを利用できません。"), m_guiTab));
		return;
	}

	if (m_beforeEditor) { m_beforeEditor(); }

	QWidget* editor = m_plugin->hasEditor()
			? m_plugin->createEmbeddedEditor(m_guiTab)
			: nullptr;
	if (editor != nullptr)
	{
		m_guiLayout->insertWidget(0, editor, 1);
		connect(m_plugin, &Vst3Plugin::editorResized, this, [this]
		{
			if (m_contentsChanged) { m_contentsChanged(); }
		});
	}
	else
	{
		m_guiLayout->insertWidget(0,
				new QLabel(tr("This plug-in has no GUI."), m_guiTab));
	}

	if (m_contentsChanged) { m_contentsChanged(); }
}

} // namespace lmms::gui
