// SPDX-License-Identifier: GPL-3.0-or-later
#include "stylenames.h"

#include "coordinput.h"

#include <QCoreApplication>
#include <QStringList>

#include <cmath>

namespace cad {

namespace {

QString t(const char* text) { return QCoreApplication::translate("Style", text); }

bool isByLayer(const QString& upper)
{
    return upper == QLatin1String("DALAYER") || upper == QLatin1String("BYLAYER") || upper == QLatin1String("DL");
}

struct Named {
    int aci;
    const char* italian;
    const char* english;
};

const Named kNamed[] = {
    {1, "Rosso", "RED"}, {2, "Giallo", "YELLOW"}, {3, "Verde", "GREEN"}, {4, "Ciano", "CYAN"},
    {5, "Blu", "BLUE"},  {6, "Magenta", "MAGENTA"}, {7, "Bianco", "WHITE"},
};

} // namespace

QString colorName(const std::optional<Color>& color)
{
    if (!color)
        return t("DaLayer");
    if (const auto aci = exactAci(*color)) {
        for (const Named& n : kNamed)
            if (n.aci == *aci)
                return t(n.italian);
        return QString::number(*aci);
    }
    return QStringLiteral("%1,%2,%3").arg(color->r).arg(color->g).arg(color->b);
}

std::optional<std::optional<Color>> parseColor(const QString& text)
{
    const QString upper = text.trimmed().toUpper();
    if (isByLayer(upper))
        return std::optional<Color>{};
    for (const Named& n : kNamed)
        if (upper == QString::fromUtf8(n.italian).toUpper() || upper == QLatin1String(n.english))
            return std::optional<Color>{aciToColor(n.aci)};
    bool ok = false;
    const int aci = upper.toInt(&ok);
    if (ok)
        return aci >= 1 && aci <= 255 ? std::optional<std::optional<Color>>{aciToColor(aci)} : std::nullopt;
    const QStringList parts = upper.split(QLatin1Char(','));
    if (parts.size() != 3)
        return std::nullopt;
    int rgb[3];
    for (int i = 0; i < 3; ++i) {
        rgb[i] = parts[i].trimmed().toInt(&ok);
        if (!ok || rgb[i] < 0 || rgb[i] > 255)
            return std::nullopt;
    }
    return std::optional<Color>{Color{static_cast<std::uint8_t>(rgb[0]), static_cast<std::uint8_t>(rgb[1]), static_cast<std::uint8_t>(rgb[2])}};
}

QString linetypeName(const std::string& name)
{
    const std::string key = linetypeKey(name);
    return key == kByLayer ? t("DaLayer") : QString::fromStdString(key);
}

QString lineweightName(int weight)
{
    if (weight == kLineweightByLayer)
        return t("DaLayer");
    if (weight < 0)
        return t("Predefinito");
    return QStringLiteral("%1 mm").arg(weight / 100.0, 0, 'f', 2);
}

std::optional<int> parseLineweight(const QString& text)
{
    QString upper = text.trimmed().toUpper();
    if (isByLayer(upper))
        return kLineweightByLayer;
    if (upper == QLatin1String("PREDEFINITO") || upper == QLatin1String("P") || upper == QLatin1String("DEFAULT"))
        return kLineweightDefault;
    if (upper.endsWith(QLatin1String("MM")))
        upper = upper.chopped(2).trimmed();
    const auto mm = parseNumber(upper.toStdString());
    if (!mm || *mm < 0.0 || *mm > 2.11 + 1e-9)
        return std::nullopt;
    return nearestLineweight(static_cast<int>(std::lround(*mm * 100.0)));
}

} // namespace cad
