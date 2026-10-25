// SPDX-License-Identifier: GPL-3.0-or-later
#include "meshexport.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>

using namespace cad;

namespace {

Model3D cube()
{
    Wall w;
    w.start = {0, 0};
    w.end = {1000, 0};
    w.thickness = 200;
    w.height = 500;
    Opening win;
    win.kind = Opening::Kind::Window;
    win.position = 500;
    win.width = 400;
    win.height = 200;
    win.sill = 100;
    w.openings.push_back(win);
    return buildingModel({w});
}

} // namespace

TEST(MeshExport, ObjWithMaterials)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("muro.obj"));
    QString error;
    ASSERT_TRUE(io::saveObj(cube(), path, &error)) << error.toStdString();
    ASSERT_TRUE(QFile::exists(dir.filePath(QStringLiteral("muro.mtl"))));

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    int vertices = 0, faces = 0, maxIndex = 0;
    double maxY = 0.0;
    QStringList materials;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QStringList f = in.readLine().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (f.isEmpty())
            continue;
        if (f[0] == QLatin1String("v")) {
            ++vertices;
            maxY = std::max(maxY, f[2].toDouble());
        } else if (f[0] == QLatin1String("f")) {
            ++faces;
            for (int k = 1; k < f.size(); ++k)
                maxIndex = std::max(maxIndex, f[k].toInt());
        } else if (f[0] == QLatin1String("usemtl")) {
            materials << f[1];
        }
    }
    EXPECT_EQ(maxIndex, vertices);  // every index points to a vertex
    EXPECT_EQ(static_cast<size_t>(faces), cube().triangles.size());
    EXPECT_DOUBLE_EQ(maxY, 500.0);  // Y-up
    EXPECT_TRUE(materials.contains(QStringLiteral("muro")));
    EXPECT_TRUE(materials.contains(QStringLiteral("vetro")));
}

TEST(MeshExport, BinaryStlWithoutGlass)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("muro.stl"));
    const Model3D model = cube();
    ASSERT_TRUE(io::saveStl(model, path));
    size_t solid = 0;
    for (const auto& t : model.triangles)
        if (t.material != Model3D::Material::Glass)
            ++solid;
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    ASSERT_GE(data.size(), 84);
    quint32 count = 0;
    std::memcpy(&count, data.constData() + 80, 4);
    EXPECT_EQ(count, solid);
    EXPECT_EQ(static_cast<size_t>(data.size()), 84 + 50 * solid);
}
