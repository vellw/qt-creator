/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "toolchain.h"

#include "abi.h"
#include "headerpath.h"
#include "toolchainmanager.h"
#include "task.h"

#include <utils/fileutils.h>
#include <utils/qtcassert.h>

#include <QCoreApplication>
#include <QUuid>

static const char ID_KEY[] = "ProjectExplorer.ToolChain.Id";
static const char DISPLAY_NAME_KEY[] = "ProjectExplorer.ToolChain.DisplayName";
static const char AUTODETECT_KEY[] = "ProjectExplorer.ToolChain.Autodetect";
static const char LANGUAGE_KEY[] = "ProjectExplorer.ToolChain.Language";

namespace ProjectExplorer {
namespace Internal {

// --------------------------------------------------------------------------
// ToolChainPrivate
// --------------------------------------------------------------------------

class ToolChainPrivate
{
public:
    typedef ToolChain::Detection Detection;

    explicit ToolChainPrivate(Core::Id typeId, Detection d) :
        m_id(QUuid::createUuid().toByteArray()),
        m_typeId(typeId),
        m_detection(d)
    {
        QTC_ASSERT(m_typeId.isValid(), return);
        QTC_ASSERT(!m_typeId.toString().contains(QLatin1Char(':')), return);
    }

    QByteArray m_id;
    QSet<ToolChain::Language> m_supportedLanguages;
    mutable QString m_displayName;
    Core::Id m_typeId;
    ToolChain::Language m_language = ToolChain::Language::None;
    Detection m_detection;
};

} // namespace Internal

/*!
    \class ProjectExplorer::ToolChain
    \brief The ToolChain class represents a tool chain.
    \sa ProjectExplorer::ToolChainManager
*/

// --------------------------------------------------------------------------

ToolChain::ToolChain(Core::Id typeId, Detection d) :
    d(new Internal::ToolChainPrivate(typeId, d))
{
}

ToolChain::ToolChain(const ToolChain &other) :
    d(new Internal::ToolChainPrivate(other.d->m_typeId, ManualDetection))
{
    d->m_language = other.d->m_language;

    // leave the autodetection bit at false.
    d->m_displayName = QCoreApplication::translate("ProjectExplorer::ToolChain", "Clone of %1")
            .arg(other.displayName());
}

void ToolChain::setLanguage(const ToolChain::Language &l)
{
    QTC_ASSERT(d->m_language == Language::None, return);
    QTC_ASSERT(l != Language::None, return);

    d->m_language = l;
}

ToolChain::~ToolChain()
{
    delete d;
}

QString ToolChain::displayName() const
{
    if (d->m_displayName.isEmpty())
        return typeDisplayName();
    return d->m_displayName;
}

void ToolChain::setDisplayName(const QString &name)
{
    if (d->m_displayName == name)
        return;

    d->m_displayName = name;
    toolChainUpdated();
}

ToolChain::Detection ToolChain::detection() const
{
    return d->m_detection;
}

QByteArray ToolChain::id() const
{
    return d->m_id;
}

Utils::FileNameList ToolChain::suggestedMkspecList() const
{
    return Utils::FileNameList();
}

Utils::FileName ToolChain::suggestedDebugger() const
{
    return ToolChainManager::defaultDebugger(targetAbi());
}

Core::Id ToolChain::typeId() const
{
    return d->m_typeId;
}

const QSet<ToolChain::Language> &ToolChain::allLanguages()
{
    static QSet<Language> languages({ Language::C, Language::Cxx });
    return languages;
}

QString ToolChain::languageDisplayName(Language language)
{
    switch (language) {
    case Language::None:
        return QCoreApplication::translate("ProjectExplorer::ToolChain", "None");
    case Language::C:
        return QCoreApplication::translate("ProjectExplorer::ToolChain", "C");
    case Language::Cxx:
        return QCoreApplication::translate("ProjectExplorer::ToolChain", "C++");
    };
    return QString();
}

QString ToolChain::languageId(ToolChain::Language l)
{
    switch (l) {
    case Language::None:
        return "None";
    case Language::C:
        return "C";
    case Language::Cxx:
        return "C++";
    };
    return QString();
}

ToolChain::Language ToolChain::language() const
{
    return d->m_language;
}

bool ToolChain::canClone() const
{
    return true;
}

bool ToolChain::operator == (const ToolChain &tc) const
{
    if (this == &tc)
        return true;

    // We ignore displayname
    return typeId() == tc.typeId() && isAutoDetected() == tc.isAutoDetected();
}

/*!
    Used by the tool chain manager to save user-generated tool chains.

    Make sure to call this function when deriving.
*/

QVariantMap ToolChain::toMap() const
{
    QVariantMap result;
    QString idToSave = d->m_typeId.toString() + QLatin1Char(':') + QString::fromUtf8(id());
    result.insert(QLatin1String(ID_KEY), idToSave);
    result.insert(QLatin1String(DISPLAY_NAME_KEY), displayName());
    result.insert(QLatin1String(AUTODETECT_KEY), isAutoDetected());
    result.insert(QLatin1String(LANGUAGE_KEY), static_cast<int>(language()));

    return result;
}

void ToolChain::toolChainUpdated()
{
    ToolChainManager::notifyAboutUpdate(this);
}

void ToolChain::setDetection(ToolChain::Detection de)
{
    if (d->m_detection == de)
        return;
    d->m_detection = de;
    toolChainUpdated();
}

/*!
    Used by the tool chain manager to load user-generated tool chains.

    Make sure to call this function when deriving.
*/

bool ToolChain::fromMap(const QVariantMap &data)
{
    d->m_displayName = data.value(QLatin1String(DISPLAY_NAME_KEY)).toString();

    // make sure we have new style ids:
    const QString id = data.value(QLatin1String(ID_KEY)).toString();
    int pos = id.indexOf(QLatin1Char(':'));
    QTC_ASSERT(pos > 0, return false);
    d->m_typeId = Core::Id::fromString(id.left(pos));
    d->m_id = id.mid(pos + 1).toUtf8();

    const bool autoDetect = data.value(QLatin1String(AUTODETECT_KEY), false).toBool();
    d->m_detection = autoDetect ? AutoDetectionFromSettings : ManualDetection;

    bool ok;
    d->m_language
            = static_cast<Language>(data.value(QLatin1String(LANGUAGE_KEY),
                                               static_cast<int>(Language::Cxx)).toInt(&ok));
    if (!ok)
        d->m_language = Language::Cxx;

    return true;
}

/*!
    Used by the tool chain kit information to validate the kit.
*/

QList<Task> ToolChain::validateKit(const Kit *) const
{
    return QList<Task>();
}

/*!
    \class ProjectExplorer::ToolChainFactory
    \brief The ToolChainFactory class creates tool chains from settings or
    autodetects them.
*/

/*!
    \fn QString ProjectExplorer::ToolChainFactory::displayName() const = 0
    Contains the name used to display the name of the tool chain that will be
    created.
*/

/*!
    \fn QStringList ProjectExplorer::ToolChain::clangParserFlags(const QStringList &cxxflags) const = 0
    Converts tool chain specific flags to list flags that tune the libclang
    parser.
*/

/*!
    \fn bool ProjectExplorer::ToolChainFactory::canRestore(const QVariantMap &data)
    Used by the tool chain manager to restore user-generated tool chains.
*/

QList<ToolChain *> ToolChainFactory::autoDetect(const QList<ToolChain *> &alreadyKnown)
{
    Q_UNUSED(alreadyKnown);
    return QList<ToolChain *>();
}

bool ToolChainFactory::canCreate()
{
    return false;
}

ToolChain *ToolChainFactory::create(ToolChain::Language l)
{
    Q_UNUSED(l);
    return nullptr;
}

bool ToolChainFactory::canRestore(const QVariantMap &)
{
    return false;
}

ToolChain *ToolChainFactory::restore(const QVariantMap &)
{
    return nullptr;
}

static QPair<QString, QString> rawIdData(const QVariantMap &data)
{
    const QString raw = data.value(QLatin1String(ID_KEY)).toString();
    const int pos = raw.indexOf(QLatin1Char(':'));
    QTC_ASSERT(pos > 0, return qMakePair(QString::fromLatin1("unknown"), QString::fromLatin1("unknown")));
    return qMakePair(raw.mid(0, pos), raw.mid(pos + 1));
}

QByteArray ToolChainFactory::idFromMap(const QVariantMap &data)
{
    return rawIdData(data).second.toUtf8();
}

Core::Id ToolChainFactory::typeIdFromMap(const QVariantMap &data)
{
    return Core::Id::fromString(rawIdData(data).first);
}

void ToolChainFactory::autoDetectionToMap(QVariantMap &data, bool detected)
{
    data.insert(QLatin1String(AUTODETECT_KEY), detected);
}

} // namespace ProjectExplorer
