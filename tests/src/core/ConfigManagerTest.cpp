/*
 * ConfigManagerTest.cpp
 *
 * This file is part of KitsuneTone.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QtTest>

#include "ConfigManager.h"

class ConfigManagerTest : public QObject
{
	Q_OBJECT

private slots:
	void createsMissingConfigurationDirectory()
	{
		QTemporaryDir temporaryDirectory;
		QVERIFY(temporaryDirectory.isValid());

		const QString configFile =
			temporaryDirectory.filePath("missing/parent/kitsunetone-config.xml");
		QVERIFY(!QFileInfo::exists(QFileInfo(configFile).absolutePath()));

		lmms::ConfigManager::inst()->loadConfigFile(configFile);
		lmms::ConfigManager::inst()->saveConfigFile();

		QVERIFY(QFileInfo::exists(configFile));
		QVERIFY(QFileInfo(configFile).size() > 0);
	}
};

QTEST_GUILESS_MAIN(ConfigManagerTest)
#include "ConfigManagerTest.moc"
