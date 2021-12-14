/**
 * SPDX-FileCopyrightText: 2020 Tobias Fella <fella@posteo.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#pragma once

#include <QObject>

#include "channeldata.h"
#include "programdata.h"

#include <QMap>
#include <QSqlQuery>
#include <QString>
#include <QVector>

class QSqlQuery;

class Database : public QObject
{
    Q_OBJECT

public:
    static Database &instance()
    {
        static Database _instance;
        return _instance;
    }
    bool execute(QSqlQuery &query);
    bool execute(const QString &query);
    Q_INVOKABLE void addCountry(const QString &id, const QString &name, const QString &url);
    Q_INVOKABLE void addChannel(const ChannelData &data, const QString &country);
    Q_INVOKABLE void addProgram(const QString &id,
                                const QString &url,
                                const QString &channelId,
                                const QDateTime &startTime,
                                const QDateTime &stopTime,
                                const QString &title,
                                const QString &subtitle,
                                const QString &description,
                                const QString &category);
    Q_INVOKABLE void updateProgramDescription(const QString &id, const QString &description);
    Q_INVOKABLE void addPrograms(const QVector<ProgramData> &programs);
    Q_INVOKABLE QVector<QString> favoriteChannels();
    Q_INVOKABLE bool programExists(const QString &channelId, qint64 lastTime);
    Q_INVOKABLE size_t programCount(const QString &channelId);
    Q_INVOKABLE QMap<QString, QVector<ProgramData>> programs();
    Q_INVOKABLE QVector<ProgramData> programs(const QString &channelId);

Q_SIGNALS:
    void countryAdded(const QString &url);
    void channelAdded(const QString &url);
    void countryDetailsUpdated(const QString &id);
    void channelDetailsUpdated(const QString &id, bool favorite);

private:
    Database();
    ~Database();
    int version();
    bool createTables();
    void cleanup();

    QSqlQuery *m_addCountryQuery;
    QSqlQuery *m_addCountryChannelQuery;
    QSqlQuery *m_addChannelQuery;
    QSqlQuery *m_addProgramQuery;
    QSqlQuery *m_updateProgramDescriptionQuery;
    QSqlQuery *m_programExistsQuery;
    QSqlQuery *m_programCountQuery;
    QSqlQuery *m_programsQuery;
    QSqlQuery *m_programsPerChannelQuery;
};
