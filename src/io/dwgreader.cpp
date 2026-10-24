// SPDX-License-Identifier: GPL-3.0-or-later
#include "dwgreader.h"

#include "dxf.h"

#include <QFile>
#include <QTemporaryFile>

#ifdef CAD4ALL_HAVE_DWG
#include "dwg_shim.h"
#endif

#include <cstdio>

namespace cad::io {

bool dwgSupported()
{
#ifdef CAD4ALL_HAVE_DWG
    return true;
#else
    return false;
#endif
}

bool fromDwg(const QByteArray& data, Document& doc, QString* message)
{
    auto fail = [&](const QString& text) {
        if (message)
            *message = text;
        return false;
    };
    if (!data.startsWith("AC") && !data.startsWith("MC"))
        return fail(QStringLiteral("Il file non è un DWG."));
#ifdef CAD4ALL_HAVE_DWG
    // LibreDWG writes the DXF to a C stream: a temporary file opened by its
    // native name, so paths with accents work on Windows too.
    QTemporaryFile temp;
    if (!temp.open())
        return fail(QStringLiteral("Impossibile creare un file temporaneo."));
    temp.close();
#ifdef _WIN32
    FILE* out = _wfopen(reinterpret_cast<const wchar_t*>(temp.fileName().utf16()), L"wb");
#else
    FILE* out = std::fopen(QFile::encodeName(temp.fileName()).constData(), "wb");
#endif
    if (!out)
        return fail(QStringLiteral("Impossibile creare un file temporaneo."));
    const int error = cad4all_dwg_to_dxf(reinterpret_cast<const unsigned char*>(data.constData()),
                                         static_cast<size_t>(data.size()), out);
    std::fclose(out);
    if (error != 0)
        return fail(QStringLiteral("Il DWG non può essere letto (errore 0x%1): versione non supportata o file danneggiato.")
                        .arg(error, 0, 16));
    QFile dxf(temp.fileName());
    if (!dxf.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Impossibile leggere la conversione del DWG."));
    return fromDxf(dxf.readAll(), doc, message);
#else
    (void)doc;
    return fail(QStringLiteral("Questa versione di CAD4All è stata compilata senza la lettura dei DWG."));
#endif
}

bool loadDwg(Document& doc, const QString& path, QString* message)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (message)
            *message = file.errorString();
        return false;
    }
    return fromDwg(file.readAll(), doc, message);
}

} // namespace cad::io
