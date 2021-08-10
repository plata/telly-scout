/**
 * SPDX-FileCopyrightText: 2020 Tobias Fella <fella@posteo.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include <KLocalizedString>
#include <QDateTime>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "TellyScoutSettings.h"
#include "database.h"
#include "fetcher.h"

#define TRUE_OR_RETURN(x)                                                                                                                                      \
    if (!x)                                                                                                                                                    \
        return false;

Database::Database()
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    QString databasePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(databasePath).mkpath(databasePath);
    db.setDatabaseName(databasePath + QStringLiteral("/database.db3"));
    db.open();

    if (!createTables()) {
        qCritical() << "Failed to create database";
    }

    cleanup();
}

bool Database::createTables()
{
    qDebug() << "Create DB tables";
    TRUE_OR_RETURN(
        execute(QStringLiteral("CREATE TABLE IF NOT EXISTS Channels (name TEXT, url TEXT, image TEXT, link TEXT, description TEXT, deleteAfterCount INTEGER, "
                               "deleteAfterType INTEGER, subscribed INTEGER, lastUpdated INTEGER, notify BOOL);")));
    TRUE_OR_RETURN(
        execute(QStringLiteral("CREATE TABLE IF NOT EXISTS Programs (channel TEXT, id TEXT UNIQUE, title TEXT, content TEXT, created INTEGER, updated INTEGER, "
                               "link TEXT, read bool);")));
    TRUE_OR_RETURN(execute(QStringLiteral("CREATE TABLE IF NOT EXISTS Countries (channel TEXT, id TEXT, name TEXT, url TEXT, email TEXT);")));
    TRUE_OR_RETURN(execute(
        QStringLiteral("CREATE TABLE IF NOT EXISTS Enclosures (channel TEXT, id TEXT, duration INTEGER, size INTEGER, title TEXT, type STRING, url STRING);")));
    TRUE_OR_RETURN(execute(QStringLiteral("PRAGMA user_version = 1;")));

    TRUE_OR_RETURN(execute(QStringLiteral("CREATE TABLE IF NOT EXISTS ChannelGroups (name TEXT NOT NULL, description TEXT, defaultGroup INTEGER);")));
    TRUE_OR_RETURN(execute(QStringLiteral("ALTER TABLE Channels ADD COLUMN groupName TEXT;")));
    TRUE_OR_RETURN(execute(QStringLiteral("ALTER TABLE Channels ADD COLUMN displayName TEXT;")));
    auto dg = i18n("Default");
    TRUE_OR_RETURN(execute(QStringLiteral("INSERT INTO ChannelGroups VALUES ('%1', '%2', 1);").arg(dg, i18n("Default Channel Group"))));
    TRUE_OR_RETURN(execute(QStringLiteral("UPDATE Channels SET groupName = '%1';").arg(dg)));
    TRUE_OR_RETURN(execute(QStringLiteral("PRAGMA user_version = 2;")));
    return true;
}

bool Database::execute(const QString &query)
{
    QSqlQuery q;
    q.prepare(query);
    return execute(q);
}

bool Database::execute(QSqlQuery &query)
{
    if (!query.exec()) {
        qWarning() << "Failed to execute SQL Query";
        qWarning() << query.lastQuery();
        qWarning() << query.lastError();
        return false;
    }
    return true;
}

int Database::version()
{
    QSqlQuery query;
    query.prepare(QStringLiteral("PRAGMA user_version;"));
    execute(query);
    if (query.next()) {
        bool ok;
        int value = query.value(0).toInt(&ok);
        qDebug() << "Database version " << value;
        if (ok) {
            return value;
        }
    } else {
        qCritical() << "Failed to check database version";
    }
    return -1;
}

void Database::cleanup()
{
    TellyScoutSettings settings;
    int count = settings.deleteAfterCount();
    int type = settings.deleteAfterType();

    if (type == 0) { // Never delete Programs
        return;
    }

    if (type == 1) { // Delete after <count> posts per channel
        // TODO
    } else {
        QDateTime dateTime = QDateTime::currentDateTime();
        if (type == 2) {
            dateTime = dateTime.addDays(-count);
        } else if (type == 3) {
            dateTime = dateTime.addDays(-7 * count);
        } else if (type == 4) {
            dateTime = dateTime.addMonths(-count);
        }
        qint64 sinceEpoch = dateTime.toSecsSinceEpoch();

        QSqlQuery query;
        query.prepare(QStringLiteral("DELETE FROM Programs WHERE updated < :sinceEpoch;"));
        query.bindValue(QStringLiteral(":sinceEpoch"), sinceEpoch);
        execute(query);
    }
}

bool Database::channelExists(const QString &url)
{
    QSqlQuery query;
    query.prepare(QStringLiteral("SELECT COUNT (url) FROM Channels WHERE url=:url;"));
    query.bindValue(QStringLiteral(":url"), url);
    Database::instance().execute(query);
    query.next();
    return query.value(0).toInt() != 0;
}

void Database::addChannel(const QString &url, const QString &groupName)
{
    qDebug() << "Adding channel";
    if (channelExists(url)) {
        qDebug() << "Channel already exists";
        return;
    }
    qDebug() << "Channel does not yet exist";

    QUrl urlFromInput = QUrl::fromUserInput(url);
    QSqlQuery query;
    query.prepare(
        QStringLiteral("INSERT INTO Channels VALUES (:name, :url, :image, :link, :description, :deleteAfterCount, :deleteAfterType, :subscribed, :lastUpdated, "
                       ":notify, :groupName, :displayName);"));
    query.bindValue(QStringLiteral(":name"), urlFromInput.toString());
    query.bindValue(QStringLiteral(":url"), urlFromInput.toString());
    query.bindValue(QStringLiteral(":image"), QLatin1String(""));
    query.bindValue(QStringLiteral(":link"), QLatin1String(""));
    query.bindValue(QStringLiteral(":description"), QLatin1String(""));
    query.bindValue(QStringLiteral(":deleteAfterCount"), 0);
    query.bindValue(QStringLiteral(":deleteAfterType"), 0);
    query.bindValue(QStringLiteral(":subscribed"), QDateTime::currentDateTime().toSecsSinceEpoch());
    query.bindValue(QStringLiteral(":lastUpdated"), 0);
    query.bindValue(QStringLiteral(":notify"), false);
    query.bindValue(QStringLiteral(":groupName"), groupName.isEmpty() ? defaultGroup() : groupName);
    query.bindValue(QStringLiteral(":displayName"), QLatin1String(""));
    execute(query);

    Q_EMIT channelAdded(urlFromInput.toString());

    Fetcher::instance().fetchChannel(urlFromInput.toString(), urlFromInput.toString()); // TODO: url -> ID
}

void Database::importChannels(const QString &path)
{
    QUrl url(path);
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    file.open(QIODevice::ReadOnly);

    QXmlStreamReader xmlReader(&file);
    while (!xmlReader.atEnd()) {
        xmlReader.readNext();
        if (xmlReader.tokenType() == 4 && xmlReader.attributes().hasAttribute(QStringLiteral("xmlUrl"))) {
            addChannel(xmlReader.attributes().value(QStringLiteral("xmlUrl")).toString());
        }
    }
    Fetcher::instance().fetchAll();
}

void Database::exportChannels(const QString &path)
{
    QUrl url(path);
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    file.open(QIODevice::WriteOnly);

    QXmlStreamWriter xmlWriter(&file);
    xmlWriter.setAutoFormatting(true);
    xmlWriter.writeStartDocument(QStringLiteral("1.0"));
    xmlWriter.writeStartElement(QStringLiteral("opml"));
    xmlWriter.writeEmptyElement(QStringLiteral("head"));
    xmlWriter.writeStartElement(QStringLiteral("body"));
    xmlWriter.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    QSqlQuery query;
    query.prepare(QStringLiteral("SELECT url, name FROM Channels;"));
    execute(query);
    while (query.next()) {
        xmlWriter.writeEmptyElement(QStringLiteral("outline"));
        xmlWriter.writeAttribute(QStringLiteral("xmlUrl"), query.value(0).toString());
        xmlWriter.writeAttribute(QStringLiteral("title"), query.value(1).toString());
    }
    xmlWriter.writeEndElement();
    xmlWriter.writeEndElement();
    xmlWriter.writeEndDocument();
}

void Database::addChannelGroup(const QString &name, const QString &description, const int isDefault)
{
    if (channelGroupExists(name)) {
        qDebug() << "Channel group already exists, nothing to add";
        return;
    }

    QSqlQuery query;
    query.prepare(QStringLiteral("INSERT INTO ChannelGroups VALUES (:name, :desc, :isDefault);"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":desc"), description);
    query.bindValue(QStringLiteral(":isDefault"), isDefault);
    execute(query);

    Q_EMIT channelGroupsUpdated();
}

void Database::editChannel(const QString &url, const QString &displayName, const QString &groupName)
{
    QSqlQuery query;
    query.prepare(QStringLiteral("UPDATE Channels SET displayName = :displayName, groupName = :groupName WHERE url = :url;"));
    query.bindValue(QStringLiteral(":displayName"), displayName);
    query.bindValue(QStringLiteral(":groupName"), groupName);
    query.bindValue(QStringLiteral(":url"), url);
    execute(query);

    Q_EMIT channelDetailsUpdated(url, displayName, groupName);
}

void Database::removeChannelGroup(const QString &name)
{
    clearChannelGroup(name);

    QSqlQuery query;
    query.prepare(QStringLiteral("DELETE FROM ChannelGroups WHERE name = :name;"));
    query.bindValue(QStringLiteral(":name"), name);
    execute(query);

    Q_EMIT channelGroupRemoved(name);
}

bool Database::channelGroupExists(const QString &name)
{
    QSqlQuery query;
    query.prepare(QStringLiteral("SELECT COUNT (1) FROM ChannelGroups WHERE name = :name;"));
    query.bindValue(QStringLiteral(":name"), name);
    Database::instance().execute(query);
    query.next();
    return (query.value(0).toInt() != 0);
}

void Database::clearChannelGroup(const QString &name)
{
    QSqlQuery query;
    query.prepare(QStringLiteral("UPDATE Channels SET groupName = NULL WHERE groupName = :name;"));
    query.bindValue(QStringLiteral(":name"), name);
    execute(query);
}

QString Database::defaultGroup()
{
    QSqlQuery query;
    query.prepare(QStringLiteral("SELECT Name FROM ChannelGroups WHERE defaultGroup = 1"));
    execute(query);

    if (query.next()) {
        return query.value(0).toString();
    } else {
        auto dg = i18n("Default");
        addChannelGroup(dg, i18n("Default Channel Group"), 1);
        return dg;
    }
}

void Database::setDefaultGroup(const QString &name)
{
    if (execute(QStringLiteral("UPDATE ChannelGroups SET defaultGroup = 0;"))) {
        QSqlQuery query;
        query.prepare(QStringLiteral("UPDATE ChannelGroups SET defaultGroup = 1 WHERE name = :name ;"));
        query.bindValue(QStringLiteral(":name"), name);
        execute(query);

        Q_EMIT channelGroupsUpdated();
    }
}
