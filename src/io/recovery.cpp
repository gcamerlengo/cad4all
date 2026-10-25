// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery.h"

#include "nativeformat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace cad::io {

namespace {

// Every session writes <id>.c4a (the drawing), <id>.ini (where it came
// from) and holds <id>.lock while it runs.
QString file(const QString& dir, const QString& id, const char* suffix)
{
    return QDir(dir).filePath(id + QLatin1String(suffix));
}

} // namespace

QString defaultRecoveryDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("recupero"));
}

std::vector<RecoveryFile> findRecoveryFiles(const QString& dir)
{
    std::vector<RecoveryFile> found;
    const QDir folder(dir);
    for (const QFileInfo& info : folder.entryInfoList({QStringLiteral("*.c4a")}, QDir::Files)) {
        const QString id = info.completeBaseName();
        // A lock that can be taken belongs to no running program; a lock
        // left by a crash is stale and taken over.
        QLockFile lock(file(dir, id, ".lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
            continue;
        lock.unlock();
        const QSettings ini(file(dir, id, ".ini"), QSettings::IniFormat);
        RecoveryFile rf;
        rf.path = info.absoluteFilePath();
        rf.source = ini.value(QStringLiteral("origine")).toString();
        rf.dwgSource = ini.value(QStringLiteral("dwg")).toString();
        rf.saved = ini.value(QStringLiteral("salvato"), info.lastModified()).toDateTime();
        found.push_back(rf);
    }
    std::sort(found.begin(), found.end(), [](const RecoveryFile& a, const RecoveryFile& b) { return a.saved > b.saved; });
    return found;
}

void removeRecoveryFile(const RecoveryFile& rf)
{
    const QFileInfo info(rf.path);
    const QString dir = info.absolutePath();
    const QString id = info.completeBaseName();
    QFile::remove(rf.path);
    QFile::remove(file(dir, id, ".ini"));
}

RecoverySession::RecoverySession(QString dir)
    : dir_(std::move(dir)), id_(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

RecoverySession::~RecoverySession() { clear(); }

QString RecoverySession::path() const { return file(dir_, id_, ".c4a"); }

bool RecoverySession::save(const Document& doc, const QString& source, const QString& dwgSource, QString* error)
{
    if (!lock_) {
        if (!QDir().mkpath(dir_)) {
            if (error)
                *error = QStringLiteral("Impossibile creare la cartella %1").arg(dir_);
            return false;
        }
        lock_ = std::make_unique<QLockFile>(file(dir_, id_, ".lock"));
        lock_->setStaleLockTime(0);
        if (!lock_->tryLock(0)) {
            lock_.reset();
            if (error)
                *error = QStringLiteral("Impossibile bloccare il file di recupero");
            return false;
        }
    }
    if (!saveNative(doc, path(), error))
        return false;
    QSettings ini(file(dir_, id_, ".ini"), QSettings::IniFormat);
    ini.setValue(QStringLiteral("origine"), source);
    ini.setValue(QStringLiteral("dwg"), dwgSource);
    ini.setValue(QStringLiteral("salvato"), QDateTime::currentDateTime());
    ini.sync();
    return true;
}

void RecoverySession::clear()
{
    if (!lock_)
        return;
    QFile::remove(path());
    QFile::remove(file(dir_, id_, ".ini"));
    lock_.reset();  // unlocks and removes the lock file
}

} // namespace cad::io
