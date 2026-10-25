// SPDX-License-Identifier: GPL-3.0-or-later
// Automatic saves (AutoCAD's SAVETIME) and their recovery after a crash.
#pragma once

#include "document.h"

#include <QDateTime>
#include <QString>

#include <memory>
#include <vector>

class QLockFile;

namespace cad::io {

// The folder of the automatic saves, in the user's application data.
QString defaultRecoveryDir();

// A copy of a drawing left by a session that did not close: the program
// crashed or the computer was switched off.
struct RecoveryFile {
    QString path;       // the .c4a copy
    QString source;     // the file the drawing came from, if any
    QString dwgSource;  // the DWG it was read from, if any
    QDateTime saved;
};

// Recovery files whose session is no longer running, the newest first.
std::vector<RecoveryFile> findRecoveryFiles(const QString& dir = defaultRecoveryDir());
// Deletes the copy and its description.
void removeRecoveryFile(const RecoveryFile& file);

// The automatic saves of one running window. Its files are locked while it
// lives and removed when it is destroyed, so only a session that ended
// badly leaves them behind.
class RecoverySession {
public:
    explicit RecoverySession(QString dir = defaultRecoveryDir());
    ~RecoverySession();
    RecoverySession(const RecoverySession&) = delete;
    RecoverySession& operator=(const RecoverySession&) = delete;

    bool save(const Document& doc, const QString& source, const QString& dwgSource, QString* error = nullptr);
    // Removes the copy, when the drawing is saved or closed.
    void clear();
    QString path() const;

private:
    QString dir_;
    QString id_;
    std::unique_ptr<QLockFile> lock_;
};

} // namespace cad::io
