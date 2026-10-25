// SPDX-License-Identifier: GPL-3.0-or-later
#include "nativeformat.h"
#include "recovery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace cad;

namespace {

Document drawing()
{
    Document doc;
    doc.add(Line{{0, 0}, {1000, 0}});
    doc.add(Circle{{0, 0}, 250});
    return doc;
}

// What a crash leaves: the copy and its description, and a lock whose
// program is gone.
void copyAsCrashed(const QString& dir, const QString& from, const QString& id, bool withLock)
{
    const QString base = QFileInfo(from).completeBaseName();
    ASSERT_TRUE(QFile::copy(from, QDir(dir).filePath(id + ".c4a")));
    ASSERT_TRUE(QFile::copy(QDir(dir).filePath(base + ".ini"), QDir(dir).filePath(id + ".ini")));
    if (withLock) {
        QFile lock(QDir(dir).filePath(id + ".lock"));
        ASSERT_TRUE(lock.open(QIODevice::WriteOnly));
        lock.write("4194300\nCAD4All\n" + QSysInfo::machineHostName().toUtf8() + "\n");
    }
}

} // namespace

TEST(Recovery, RunningSessionIsNotOffered)
{
    QTemporaryDir dir;
    io::RecoverySession session(dir.path());
    ASSERT_TRUE(session.save(drawing(), QStringLiteral("/progetti/casa.c4a"), QString()));
    EXPECT_TRUE(QFile::exists(session.path()));
    EXPECT_TRUE(io::findRecoveryFiles(dir.path()).empty());
}

TEST(Recovery, ClosedSessionLeavesNothing)
{
    QTemporaryDir dir;
    {
        io::RecoverySession session(dir.path());
        ASSERT_TRUE(session.save(drawing(), QString(), QString()));
    }
    EXPECT_TRUE(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    EXPECT_TRUE(io::findRecoveryFiles(dir.path()).empty());
}

TEST(Recovery, ClearRemovesTheCopy)
{
    QTemporaryDir dir;
    io::RecoverySession session(dir.path());
    ASSERT_TRUE(session.save(drawing(), QString(), QString()));
    session.clear();
    EXPECT_TRUE(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    // It saves again after a clear.
    ASSERT_TRUE(session.save(drawing(), QString(), QString()));
    EXPECT_TRUE(QFile::exists(session.path()));
}

TEST(Recovery, CrashedSessionIsOffered)
{
    QTemporaryDir dir;
    io::RecoverySession session(dir.path());
    ASSERT_TRUE(session.save(drawing(), QStringLiteral("/progetti/casa.dxf"), QStringLiteral("/progetti/rilievo.dwg")));
    copyAsCrashed(dir.path(), session.path(), QStringLiteral("senza-lock"), false);
    copyAsCrashed(dir.path(), session.path(), QStringLiteral("lock-vecchio"), true);

    const auto found = io::findRecoveryFiles(dir.path());
    ASSERT_EQ(found.size(), 2u);
    for (const io::RecoveryFile& rf : found) {
        EXPECT_EQ(rf.source, QStringLiteral("/progetti/casa.dxf"));
        EXPECT_EQ(rf.dwgSource, QStringLiteral("/progetti/rilievo.dwg"));
        EXPECT_TRUE(rf.saved.isValid());
        Document loaded;
        ASSERT_TRUE(io::loadNative(loaded, rf.path));
        EXPECT_EQ(loaded.entities().size(), 2u);
    }

    io::removeRecoveryFile(found.front());
    EXPECT_EQ(io::findRecoveryFiles(dir.path()).size(), 1u);
}
