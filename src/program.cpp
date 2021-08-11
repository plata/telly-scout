/**
 * SPDX-FileCopyrightText: 2020 Tobias Fella <fella@posteo.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "program.h"

#include <QRegularExpression>
#include <QSqlQuery>
#include <QUrl>

#include "database.h"

Program::Program(Channel *channel, int index)
    : QObject(nullptr)
    , m_channel(channel)
{
    QSqlQuery programQuery;
    programQuery.prepare(QStringLiteral("SELECT * FROM Programs WHERE channel=:channel ORDER BY start DESC LIMIT 1 OFFSET :index;"));
    programQuery.bindValue(QStringLiteral(":channel"), m_channel->url());
    programQuery.bindValue(QStringLiteral(":index"), index);
    Database::instance().execute(programQuery);
    if (!programQuery.next()) {
        qWarning() << "No element with index" << index << "found in channel" << m_channel->url();
    }

    QSqlQuery countryQuery;
    countryQuery.prepare(QStringLiteral("SELECT * FROM Countries WHERE id=:id"));
    countryQuery.bindValue(QStringLiteral(":id"), programQuery.value(QStringLiteral("id")).toString());
    Database::instance().execute(countryQuery);

    while (countryQuery.next()) {
        m_countries += new Country(countryQuery.value(QStringLiteral("name")).toString(), countryQuery.value(QStringLiteral("url")).toString(), nullptr);
    }

    m_created.setSecsSinceEpoch(programQuery.value(QStringLiteral("start")).toInt());
    m_updated.setSecsSinceEpoch(programQuery.value(QStringLiteral("stop")).toInt());

    m_id = programQuery.value(QStringLiteral("id")).toString();
    m_title = programQuery.value(QStringLiteral("title")).toString();
    m_content = programQuery.value(QStringLiteral("description")).toString();
    m_link = programQuery.value(QStringLiteral("subtitle")).toString();
}

Program::~Program()
{
    qDeleteAll(m_countries);
}

QString Program::id() const
{
    return m_id;
}

QString Program::title() const
{
    return m_title;
}

QString Program::content() const
{
    return m_content;
}

QVector<Country *> Program::countries() const
{
    return m_countries;
}

QDateTime Program::created() const
{
    return m_created;
}

QDateTime Program::updated() const
{
    return m_updated;
}

QString Program::link() const
{
    return m_link;
}

QString Program::baseUrl() const
{
    return QUrl(m_link).adjusted(QUrl::RemovePath).toString();
}

QString Program::adjustedContent(int width, int fontSize)
{
    QString ret(m_content);
    QRegularExpression imgRegex(QStringLiteral("<img ((?!width=\"[0-9]+(px)?\").)*(width=\"([0-9]+)(px)?\")?[^>]*>"));

    QRegularExpressionMatchIterator i = imgRegex.globalMatch(ret);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();

        QString imgTag(match.captured());
        if (imgTag.contains(QStringLiteral("wp-smiley"))) {
            imgTag.insert(4, QStringLiteral(" width=\"%1\"").arg(fontSize));
        }

        QString widthParameter = match.captured(4);

        if (widthParameter.length() != 0) {
            if (widthParameter.toInt() > width) {
                imgTag.replace(match.captured(3), QStringLiteral("width=\"%1\"").arg(width));
                imgTag.replace(QRegularExpression(QStringLiteral("height=\"([0-9]+)(px)?\"")), QString());
            }
        } else {
            imgTag.insert(4, QStringLiteral(" width=\"%1\"").arg(width));
        }
        ret.replace(match.captured(), imgTag);
    }

    ret.replace(QStringLiteral("<img"), QStringLiteral("<br /> <img"));
    return ret;
}
