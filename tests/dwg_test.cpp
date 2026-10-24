// SPDX-License-Identifier: GPL-3.0-or-later
#include "dwgreader.h"

#include <QFile>

#include <gtest/gtest.h>

using namespace cad;

TEST(Dwg, RejectsOtherFiles)
{
    Document doc;
    QString message;
    EXPECT_FALSE(io::fromDwg("0\nSECTION\n", doc, &message));
    EXPECT_FALSE(message.isEmpty());
}

#ifdef CAD4ALL_DWG_TEST_DATA

namespace {

QByteArray sample(const char* name)
{
    QFile f(QStringLiteral(CAD4ALL_DWG_TEST_DATA "/") + QLatin1String(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

template <class T>
size_t count(const Document& doc)
{
    size_t n = 0;
    for (const auto& [id, e] : doc.entities())
        n += std::holds_alternative<T>(e.shape) ? 1 : 0;
    return n;
}

} // namespace

TEST(Dwg, ReadsAutoCad2018Drawing)
{
    const QByteArray data = sample("sample_2018.dwg");
    ASSERT_FALSE(data.isEmpty());
    Document doc;
    QString message;
    ASSERT_TRUE(io::fromDwg(data, doc, &message)) << message.toStdString();
    ASSERT_NE(doc.layer("Tavolo 1"), nullptr);
    EXPECT_EQ(count<Circle>(doc), 1u);
    bool found = false;
    for (const auto& [id, e] : doc.entities())
        if (const auto* t = std::get_if<Text>(&e.shape))
            found = found || t->content == "Jen teksto simpla, cxu ne?";
    EXPECT_TRUE(found);
}

TEST(Dwg, ReadsOlderVersions)
{
    for (const char* name : {"example_r14.dwg", "example_2000.dwg", "example_2004.dwg", "example_2007.dwg",
                             "example_2010.dwg", "example_2013.dwg"}) {
        Document doc;
        QString message;
        EXPECT_TRUE(io::fromDwg(sample(name), doc, &message)) << name << ": " << message.toStdString();
        EXPECT_GT(doc.entities().size(), 20u) << name;
    }
}

#else

TEST(Dwg, ReportsMissingSupport)
{
    EXPECT_FALSE(io::dwgSupported());
}

#endif
