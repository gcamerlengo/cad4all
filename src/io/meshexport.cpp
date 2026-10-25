// SPDX-License-Identifier: GPL-3.0-or-later
#include "meshexport.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <cmath>
#include <map>
#include <tuple>

namespace cad::io {

namespace {

struct MaterialInfo {
    const char* name;
    int r, g, b;
    double opacity;
};

MaterialInfo materialInfo(Model3D::Material m)
{
    switch (m) {
    case Model3D::Material::Glass:
        return {"vetro", 150, 195, 225, 0.45};
    case Model3D::Material::Wood:
        return {"legno", 168, 122, 78, 1.0};
    case Model3D::Material::Roof:
        return {"tetto", 176, 96, 70, 1.0};
    case Model3D::Material::Wall:
        break;
    }
    return {"muro", 236, 233, 226, 1.0};
}

bool fail(QString* error, const QString& message)
{
    if (error)
        *error = message;
    return false;
}

bool degenerate(const Model3D::Triangle& t)
{
    return length(cross(t.v[1] - t.v[0], t.v[2] - t.v[0])) < 1e-12;
}

} // namespace

bool saveObj(const Model3D& model, const QString& path, QString* error)
{
    const QFileInfo info(path);
    const QString mtlName = info.completeBaseName() + QStringLiteral(".mtl");
    const QString mtlPath = info.dir().filePath(mtlName);

    QSaveFile mtl(mtlPath);
    if (!mtl.open(QIODevice::WriteOnly | QIODevice::Text))
        return fail(error, mtl.errorString());
    {
        QTextStream out(&mtl);
        out << "# CAD4All\n";
        for (auto m : {Model3D::Material::Wall, Model3D::Material::Glass, Model3D::Material::Wood, Model3D::Material::Roof}) {
            const MaterialInfo mi = materialInfo(m);
            out << "\nnewmtl " << mi.name << "\n";
            out << "Kd " << mi.r / 255.0 << ' ' << mi.g / 255.0 << ' ' << mi.b / 255.0 << "\n";
            out << "Ka 0 0 0\nKs 0 0 0\n";
            out << "d " << mi.opacity << "\n";
            out << "illum 1\n";
        }
    }
    if (!mtl.commit())
        return fail(error, mtl.errorString());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return fail(error, file.errorString());
    QTextStream out(&file);
    out.setRealNumberPrecision(10);
    out << "# CAD4All\n";
    out << "mtllib " << mtlName << "\n";
    // Shared corners are written once.
    std::map<std::tuple<double, double, double>, int> index;
    std::map<Model3D::Material, std::vector<std::array<int, 3>>> faces;
    auto vertex = [&](const Vec3& v) {
        const auto key = std::make_tuple(std::round(v.x * 1e6) / 1e6, std::round(v.y * 1e6) / 1e6, std::round(v.z * 1e6) / 1e6);
        const auto it = index.find(key);
        if (it != index.end())
            return it->second;
        const int id = static_cast<int>(index.size()) + 1;
        index.emplace(key, id);
        out << "v " << v.x << ' ' << v.z << ' ' << -v.y << "\n";
        return id;
    };
    for (const auto& t : model.triangles) {
        if (degenerate(t))
            continue;
        faces[t.material].push_back({vertex(t.v[0]), vertex(t.v[1]), vertex(t.v[2])});
    }
    for (const auto& [m, list] : faces) {
        const MaterialInfo mi = materialInfo(m);
        out << "g " << mi.name << "\nusemtl " << mi.name << "\n";
        for (const auto& f : list)
            out << "f " << f[0] << ' ' << f[1] << ' ' << f[2] << "\n";
    }
    out.flush();
    if (!file.commit())
        return fail(error, file.errorString());
    return true;
}

bool saveStl(const Model3D& model, const QString& path, QString* error)
{
    std::vector<const Model3D::Triangle*> tris;
    for (const auto& t : model.triangles)
        if (t.material != Model3D::Material::Glass && !degenerate(t))
            tris.push_back(&t);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, file.errorString());
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    QByteArray header("CAD4All");
    header.resize(80, ' ');
    out.writeRawData(header.constData(), 80);
    out << static_cast<quint32>(tris.size());
    for (const auto* t : tris) {
        const Vec3 n = normalized(cross(t->v[1] - t->v[0], t->v[2] - t->v[0]));
        out << static_cast<float>(n.x) << static_cast<float>(n.y) << static_cast<float>(n.z);
        for (const Vec3& v : t->v)
            out << static_cast<float>(v.x) << static_cast<float>(v.y) << static_cast<float>(v.z);
        out << static_cast<quint16>(0);
    }
    if (out.status() != QDataStream::Ok || !file.commit())
        return fail(error, file.errorString());
    return true;
}

} // namespace cad::io
