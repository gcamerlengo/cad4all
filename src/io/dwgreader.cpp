// SPDX-License-Identifier: GPL-3.0-or-later
#include "dwgreader.h"

#include "dxf.h"

#include <QFile>
#include <QSaveFile>
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

bool toDwg(const Document& doc, QByteArray& out, QString* error)
{
#ifdef CAD4ALL_HAVE_DWG
    const QByteArray dxf = toDxf(doc, DxfVersion::R2000);
    unsigned char* data = nullptr;
    size_t size = 0;
    const int code = cad4all_dxf_to_dwg(reinterpret_cast<const unsigned char*>(dxf.constData()),
                                        static_cast<size_t>(dxf.size()), &data, &size);
    if (code != 0) {
        if (error)
            *error = QStringLiteral("Impossibile scrivere il DWG (errore 0x%1).").arg(code, 0, 16);
        return false;
    }
    out = QByteArray(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    cad4all_dwg_free(data);
    return true;
#else
    (void)doc;
    (void)out;
    if (error)
        *error = QStringLiteral("Questa versione di CAD4All è stata compilata senza il formato DWG.");
    return false;
#endif
}

bool saveDwg(const Document& doc, const QString& path, QString* error)
{
    QByteArray data;
    if (!toDwg(doc, data, error))
        return false;
    // Written from Qt, so paths with accents work on Windows too.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace cad::io
