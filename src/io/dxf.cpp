// SPDX-License-Identifier: GPL-3.0-or-later
#include "dxf.h"

#include "building.h"
#include "hatch.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <variant>

namespace cad::io {

namespace {

// ------------------------------------------------------------------ reading

struct Group {
    int code = 0;
    QString value;
};

// One DXF object: its type (the value of group 0) and the groups after it.
struct Record {
    QString type;
    std::vector<Group> groups;

    const Group* find(int code) const
    {
        for (const Group& g : groups)
            if (g.code == code)
                return &g;
        return nullptr;
    }
    QString str(int code, const QString& fallback = {}) const
    {
        const Group* g = find(code);
        return g ? g->value : fallback;
    }
    double num(int code, double fallback = 0.0) const
    {
        const Group* g = find(code);
        if (!g)
            return fallback;
        bool ok = false;
        const double v = g->value.toDouble(&ok);
        return ok ? v : fallback;
    }
    int integer(int code, int fallback = 0) const { return static_cast<int>(num(code, fallback)); }
    Vec2 point(int code) const { return {num(code), num(code + 10)}; }
};

double toDouble(const QString& s)
{
    bool ok = false;
    const double v = s.toDouble(&ok);
    return ok ? v : 0.0;
}

// Special characters of DXF texts: %%c, %%d, %%p and \U+XXXX.
QString decodeText(QString s)
{
    s.replace(QLatin1String("%%c"), QStringLiteral("Ø"), Qt::CaseInsensitive);
    s.replace(QLatin1String("%%d"), QStringLiteral("°"), Qt::CaseInsensitive);
    s.replace(QLatin1String("%%p"), QStringLiteral("±"), Qt::CaseInsensitive);
    s.replace(QLatin1String("%%u"), QString(), Qt::CaseInsensitive);
    s.replace(QLatin1String("%%o"), QString(), Qt::CaseInsensitive);
    s.replace(QLatin1String("%%%"), QStringLiteral("%"));
    static const QRegularExpression unicode(QStringLiteral("\\\\U\\+([0-9A-Fa-f]{4})"));
    QString out;
    qsizetype last = 0;
    auto it = unicode.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        out += QChar(static_cast<char16_t>(m.captured(1).toUShort(nullptr, 16)));
        last = m.capturedEnd();
    }
    out += s.mid(last);
    return out;
}

// Plain lines of an MTEXT: formatting codes removed, \P splits lines.
QStringList mtextLines(QString s)
{
    s.replace(QLatin1String("\\\\"), QStringLiteral("\x01"));  // literal backslash
    s.replace(QLatin1String("\\{"), QStringLiteral("\x02"));
    s.replace(QLatin1String("\\}"), QStringLiteral("\x03"));
    s.replace(QLatin1String("\\~"), QStringLiteral(" "));
    // Stacked fractions \Sa^b; or \Sa/b;
    static const QRegularExpression stacked(QStringLiteral("\\\\S([^;^/#]*)[\\^/#]([^;]*);"));
    s.replace(stacked, QStringLiteral("\\1/\\2"));
    // Codes with an argument up to ';' (font, height, width, colour...).
    static const QRegularExpression withArg(QStringLiteral("\\\\[AaCcFfHhQqTtWwp][^;]*;"));
    s.replace(withArg, QString());
    // Toggles without argument (underline, overline, strike).
    static const QRegularExpression toggles(QStringLiteral("\\\\[LlOoKk]"));
    s.replace(toggles, QString());
    s.replace(QLatin1String("\\n"), QLatin1String("\\P"));
    s.remove(QLatin1Char('{'));
    s.remove(QLatin1Char('}'));
    QStringList lines = s.split(QLatin1String("\\P"));
    for (QString& line : lines) {
        line.replace(QChar(1), QLatin1Char('\\'));
        line.replace(QChar(2), QLatin1Char('{'));
        line.replace(QChar(3), QLatin1Char('}'));
        line = decodeText(line);
    }
    return lines;
}

struct BlockRecords {
    Vec2 base;
    std::vector<Record> records;
};

class DxfImporter {
public:
    explicit DxfImporter(Document& doc) : doc_(doc) {}

    bool parse(const QByteArray& data, QString* error)
    {
        if (data.startsWith("AutoCAD Binary DXF")) {
            *error = QStringLiteral("I file DXF binari non sono supportati: salvare il disegno come DXF ASCII.");
            return false;
        }
        // Text is UTF-8 from AutoCAD 2007 (AC1021) on, the Windows code page
        // before: Latin-1 covers the Italian accented letters.
        QString text = QString::fromLatin1(data);
        const qsizetype ver = text.indexOf(QLatin1String("$ACADVER"));
        if (ver >= 0) {
            static const QRegularExpression version(QStringLiteral("AC(\\d{4})"));
            const auto m = version.match(text, ver);
            if (m.hasMatch() && m.captured(1).toInt() >= 1021)
                text = QString::fromUtf8(data);
        }

        const QStringList lines = text.split(QLatin1Char('\n'));
        std::vector<Group> groups;
        groups.reserve(static_cast<size_t>(lines.size() / 2));
        for (qsizetype i = 0; i + 1 < lines.size(); i += 2) {
            bool ok = false;
            const int code = lines[i].trimmed().toInt(&ok);
            if (!ok) {
                if (lines[i].trimmed().isEmpty() && i + 2 >= lines.size())
                    break;
                *error = QStringLiteral("File DXF non valido alla riga %1.").arg(i + 1);
                return false;
            }
            QString value = lines[i + 1];
            if (value.endsWith(QLatin1Char('\r')))
                value.chop(1);
            // Texts keep their spaces; everything else is trimmed.
            if (code != 1 && code != 3)
                value = value.trimmed();
            groups.push_back({code, value});
        }
        if (groups.empty()) {
            *error = QStringLiteral("Il file DXF è vuoto.");
            return false;
        }

        // Split into sections of records.
        std::map<QString, std::vector<Record>> sections;
        QString section;
        Record* current = nullptr;
        for (size_t i = 0; i < groups.size(); ++i) {
            const Group& g = groups[i];
            if (g.code != 0) {
                // The header has no records of its own: its groups go in an unnamed one.
                if (!current && !section.isEmpty()) {
                    auto& list = sections[section];
                    list.push_back({QString(), {}});
                    current = &list.back();
                }
                if (current)
                    current->groups.push_back(g);
                continue;
            }
            if (g.value == QLatin1String("SECTION")) {
                section.clear();
                current = nullptr;
                // The name follows as group 2.
                if (i + 1 < groups.size() && groups[i + 1].code == 2) {
                    section = groups[i + 1].value;
                    ++i;
                }
                continue;
            }
            if (g.value == QLatin1String("ENDSEC") || g.value == QLatin1String("EOF")) {
                section.clear();
                current = nullptr;
                continue;
            }
            auto& list = sections[section];
            list.push_back({g.value, {}});
            current = &list.back();
        }

        readHeader(sections[QStringLiteral("HEADER")]);
        readLinetypes(sections[QStringLiteral("TABLES")]);
        readLayers(sections[QStringLiteral("TABLES")]);
        readDimStyles(sections[QStringLiteral("TABLES")]);
        readBlocks(sections[QStringLiteral("BLOCKS")]);
        // Before R12 files could have no ENTITIES section name; take what is there.
        convertList(sections[QStringLiteral("ENTITIES")], Transform{}, std::string(), 0, true);
        return true;
    }

    int imported() const { return imported_; }
    const std::map<QString, int>& skipped() const { return skipped_; }

private:
    void readHeader(const std::vector<Record>& records)
    {
        // The header is one record whose groups are pairs "9 $NAME" + value.
        for (const Record& r : records) {
            QString name;
            for (const Group& g : r.groups) {
                if (g.code == 9) {
                    name = g.value;
                    continue;
                }
                if (name == QLatin1String("$LTSCALE") && g.code == 40)
                    doc_.setLinetypeScale(toDouble(g.value));
                else if (name == QLatin1String("$DIMSCALE") && g.code == 40)
                    dimScale_ = toDouble(g.value) > 0.0 ? toDouble(g.value) : 1.0;
                else if (name == QLatin1String("$DIMTXT") && g.code == 40)
                    dimText_ = toDouble(g.value);
                else if (name == QLatin1String("$DIMASZ") && g.code == 40)
                    dimArrow_ = toDouble(g.value);
            }
        }
    }

    struct DimStyle {
        double scale = 1.0;
        double text = 0.0;
        double arrow = 0.0;
    };

    void readDimStyles(const std::vector<Record>& records)
    {
        for (const Record& r : records) {
            if (r.type != QLatin1String("DIMSTYLE"))
                continue;
            DimStyle st;
            st.scale = r.num(40, 1.0) > 0.0 ? r.num(40, 1.0) : 1.0;
            st.text = r.num(140);
            st.arrow = r.num(41);
            dimStyles_[r.str(2).toUpper()] = st;
        }
    }

    void readLayers(const std::vector<Record>& records)
    {
        for (const Record& r : records) {
            if (r.type != QLatin1String("LAYER"))
                continue;
            const QString name = r.str(2);
            if (name.isEmpty())
                continue;
            Layer l = Layer::named(name.toStdString());
            const int color = r.integer(62, 7);
            const int flags = r.integer(70);
            l.color = aciToColor(std::abs(color) == 0 ? 7 : std::abs(color));
            // A negative colour means the layer is off; frozen layers are hidden too.
            l.visible = color >= 0 && !(flags & 1);
            l.locked = flags & 4;
            const std::string linetype = linetypeKey(r.str(6, QString::fromStdString(kContinuous)).toStdString());
            l.linetype = doc_.linetype(linetype) ? linetype : kContinuous;
            const int weight = r.integer(370, kLineweightDefault);
            l.lineweight = weight >= 0 ? weight : kLineweightDefault;
            if (doc_.layer(l.name))
                doc_.updateLayer(l.name, l);
            else
                doc_.addLayer(l);
        }
    }

    void readLinetypes(const std::vector<Record>& records)
    {
        for (const Record& r : records) {
            if (r.type != QLatin1String("LTYPE"))
                continue;
            Linetype t;
            t.name = r.str(2).toStdString();
            t.description = r.str(3).toStdString();
            for (const Group& g : r.groups)
                if (g.code == 49)
                    t.pattern.push_back(toDouble(g.value));
            if (linetypeKey(t.name) == "BYBLOCK" || isStandardLinetype(t.name))
                continue;
            doc_.addLinetype(t);
        }
    }

    // Colour (62, or 420 for a true colour), linetype (6) and lineweight
    // (370) of an entity. BYBLOCK takes those of the reference around an
    // exploded block, and stays BYBLOCK in a block definition.
    EntityStyle recordStyle(const Record& r)
    {
        EntityStyle style;
        const bool keepByBlock = target_ && !exploding_;
        colorByBlock_ = false;
        const int aci = r.integer(62, 256);
        if (r.find(420)) {
            const unsigned rgb = static_cast<unsigned>(r.integer(420));
            style.color = Color{static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8), static_cast<std::uint8_t>(rgb)};
        } else if (aci == 0) {
            style.color = blockStyle_.color;
            colorByBlock_ = keepByBlock;
        } else if (aci != 256) {
            style.color = aciToColor(std::abs(aci));
        }
        const std::string linetype = linetypeKey(r.str(6, QString::fromStdString(kByLayer)).toStdString());
        if (linetype == kByBlock)
            style.linetype = keepByBlock ? kByBlock : blockStyle_.linetype;
        else if (linetype != kByLayer && doc_.linetype(linetype))
            style.linetype = linetype;
        const int weight = r.integer(370, kLineweightByLayer);
        if (weight == kLineweightByBlock)
            style.lineweight = keepByBlock ? kLineweightByBlock : blockStyle_.lineweight;
        else if (weight >= 0 || weight == kLineweightDefault)
            style.lineweight = weight;
        return style;
    }

    void readBlocks(const std::vector<Record>& records)
    {
        BlockRecords* block = nullptr;
        for (const Record& r : records) {
            if (r.type == QLatin1String("BLOCK")) {
                block = &blocks_[r.str(2)];
                block->base = r.point(10);
                continue;
            }
            if (r.type == QLatin1String("ENDBLK")) {
                block = nullptr;
                continue;
            }
            if (block)
                block->records.push_back(r);
        }
    }

    void skip(const QString& type) { ++skipped_[type]; }

    // The style of the record being converted, and of the block reference
    // around it.
    EntityStyle style_;
    EntityStyle blockStyle_;
    // Converting the objects of a block that is exploded into its reference.
    bool exploding_ = false;

    void add(const std::string& layer, const Shape& shape, const Transform& xf)
    {
        if (target_) {
            BlockItem item;
            item.shape = transformed(shape, xf);
            item.layer = layer.empty() ? std::string("0") : layer;
            item.color = colorByBlock_ ? std::nullopt : style_.color;
            item.colorByBlock = colorByBlock_;
            item.linetype = style_.linetype;
            item.lineweight = style_.lineweight;
            target_->push_back(std::move(item));
            return;
        }
        Entity e;
        e.layer = layer.empty() ? std::string("0") : layer;
        e.shape = transformed(shape, xf);
        applyStyle(e, style_);
        doc_.add(std::move(e));
        ++imported_;
    }

    void convertList(const std::vector<Record>& records, const Transform& xf, const std::string& insertLayer, int depth,
                     bool modelSpaceOnly)
    {
        for (size_t i = 0; i < records.size(); ++i) {
            const Record& r = records[i];
            if (modelSpaceOnly && r.integer(67) == 1)
                continue;  // paper space
            // Entities on layer 0 inside a block take the layer of the reference.
            std::string layer = r.str(8, QStringLiteral("0")).toStdString();
            if (!insertLayer.empty() && layer == "0")
                layer = insertLayer;
            style_ = recordStyle(r);
            // Planar entities with a downward normal are mirrored in X.
            Transform ocs;
            if (r.num(230, 1.0) < 0.0)
                ocs = Transform::mirror({0.0, 0.0}, {0.0, 1.0});
            const Transform full = xf * ocs;

            if (r.type == QLatin1String("POLYLINE")) {
                Polyline pl;
                const int flags = r.integer(70);
                pl.closed = flags & 1;
                size_t j = i + 1;
                for (; j < records.size() && records[j].type == QLatin1String("VERTEX"); ++j) {
                    const Record& v = records[j];
                    if (v.integer(70) & 16)
                        continue;  // spline frame control point
                    pl.points.push_back(v.point(10));
                    pl.setBulge(pl.points.size() - 1, v.num(42));
                }
                if (j < records.size() && records[j].type == QLatin1String("SEQEND"))
                    ++j;
                i = j - 1;
                if ((flags & (16 | 64)) || pl.points.size() < 2) {
                    skip(QStringLiteral("POLYLINE 3D"));
                    continue;
                }
                add(layer, pl, (flags & 8) ? xf : full);
                continue;
            }
            if (r.type == QLatin1String("INSERT")) {
                insert(r, xf, layer, depth);
                // Attributes follow the reference up to SEQEND.
                size_t j = i + 1;
                for (; j < records.size() && records[j].type == QLatin1String("ATTRIB"); ++j) {
                    style_ = recordStyle(records[j]);
                    text(records[j], full, layer);
                }
                if (j < records.size() && records[j].type == QLatin1String("SEQEND") && j > i + 1)
                    ++j;
                i = j - 1;
                continue;
            }
            convert(r, xf, full, layer);
        }
    }

    // The block named `name` made from its records, the first time it is used.
    std::shared_ptr<const Block> definition(const QString& name, int depth)
    {
        if (const auto it = defined_.find(name); it != defined_.end())
            return it->second;
        const auto it = blocks_.find(name);
        if (it == blocks_.end() || depth > 16 || defining_.count(name))
            return nullptr;
        defining_.insert(name);
        std::vector<BlockItem> items;
        auto* const outerTarget = target_;
        const bool outerExploding = exploding_;
        const EntityStyle outerStyle = style_;
        target_ = &items;
        exploding_ = false;
        convertList(it->second.records, Transform{}, std::string(), depth + 1, false);
        target_ = outerTarget;
        exploding_ = outerExploding;
        style_ = outerStyle;
        defining_.erase(name);
        auto block = makeBlock(name.toStdString(), it->second.base, std::move(items));
        doc_.defineBlock(block);
        defined_[name] = block;
        return block;
    }

    void insert(const Record& r, const Transform& xf, const std::string& layer, int depth)
    {
        const QString name = r.str(2);
        const auto it = blocks_.find(name);
        if (it == blocks_.end() || depth > 16) {
            skip(QStringLiteral("INSERT"));
            return;
        }
        // Named blocks stay blocks; anonymous ones (*U...) are exploded.
        if (!name.startsWith(QLatin1Char('*')) && !exploding_) {
            insertReference(r, xf, layer, depth);
            return;
        }
        const BlockRecords& block = it->second;
        // What the block's BYBLOCK entities take, for this reference.
        const EntityStyle outer = blockStyle_;
        blockStyle_ = style_;
        const double sx = r.num(41, 1.0);
        const double sy = r.num(42, 1.0);
        const double rot = degToRad(r.num(50));
        const int columns = std::max(1, r.integer(70, 1));
        const int rows = std::max(1, r.integer(71, 1));
        const double colStep = r.num(44);
        const double rowStep = r.num(45);
        Transform ocs;
        if (r.num(230, 1.0) < 0.0)
            ocs = Transform::mirror({0.0, 0.0}, {0.0, 1.0});
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < columns; ++col) {
                // Block base to origin, scale, rotate, then to the insertion point.
                Transform scale;
                scale.a = sx;
                scale.d = sy;
                const Vec2 cell = Transform::rotation({0, 0}, rot).applyVector({col * colStep, row * rowStep});
                const Transform place = Transform::translation(r.point(10) + cell) * Transform::rotation({0, 0}, rot) * scale
                                      * Transform::translation(block.base * -1.0);
                const bool outerExploding = exploding_;
                exploding_ = true;
                convertList(block.records, xf * ocs * place, layer, depth + 1, false);
                exploding_ = outerExploding;
            }
        }
        blockStyle_ = outer;
    }

    // A reference to a named block, or one per cell of a MINSERT.
    void insertReference(const Record& r, const Transform& xf, const std::string& layer, int depth)
    {
        const auto block = definition(r.str(2), depth);
        if (!block) {
            skip(QStringLiteral("INSERT"));
            return;
        }
        const EntityStyle style = style_;
        const double sx = r.num(41, 1.0);
        const double sy = r.num(42, 1.0);
        const double rot = degToRad(r.num(50));
        const int columns = std::max(1, r.integer(70, 1));
        const int rows = std::max(1, r.integer(71, 1));
        const double colStep = r.num(44);
        const double rowStep = r.num(45);
        Transform ocs;
        if (r.num(230, 1.0) < 0.0)
            ocs = Transform::mirror({0.0, 0.0}, {0.0, 1.0});
        if (std::abs(sx) < 1e-12 || std::abs(sy) < 1e-12) {
            skip(QStringLiteral("INSERT"));
            return;
        }
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < columns; ++col) {
                Transform scale;
                scale.a = sx;
                scale.d = sy;
                const Vec2 cell = Transform::rotation({0, 0}, rot).applyVector({col * colStep, row * rowStep});
                const Transform place = Transform::translation(r.point(10) + cell) * Transform::rotation({0, 0}, rot) * scale
                                      * Transform::translation(block->base * -1.0);
                style_ = style;
                add(layer, placedInsert(block, xf * ocs * place), Transform{});
            }
        }
    }

    void text(const Record& r, const Transform& xf, const std::string& layer)
    {
        Text t;
        t.content = decodeText(r.str(1)).toStdString();
        t.height = r.num(40, 2.5);
        t.rotation = degToRad(r.num(50));
        const int h = r.integer(72);
        const int v = r.integer(r.type == QLatin1String("TEXT") ? 73 : 74);
        // Any justification but the default left/baseline is anchored at 11.
        const bool aligned = (h != 0 || v != 0) && r.find(11);
        t.position = aligned ? r.point(11) : r.point(10);
        if (h == 1 || h == 4)
            t.align = Text::Align::Center;
        else if (h == 2)
            t.align = Text::Align::Right;
        // Middle and top: move down to the baseline.
        const Vec2 up = perpendicular(polar(1.0, t.rotation));
        if (v == 2 || h == 4)
            t.position = t.position - up * (t.height / 2.0);
        else if (v == 3)
            t.position = t.position - up * t.height;
        if (t.content.empty() || t.height <= 0.0)
            return;
        add(layer, t, xf);
    }

    void mtext(const Record& r, const Transform& xf, const std::string& layer)
    {
        QString raw;
        for (const Group& g : r.groups)
            if (g.code == 3)
                raw += g.value;
        raw += r.str(1);
        const QStringList lines = mtextLines(raw);
        const double height = r.num(40, 2.5);
        double rotation = degToRad(r.num(50));
        if (r.find(11))
            rotation = angleOf(r.point(11));
        const int attach = std::clamp(r.integer(71, 1), 1, 9);
        const Text::Align align = (attach - 1) % 3 == 0 ? Text::Align::Left
                                : (attach - 1) % 3 == 1 ? Text::Align::Center
                                                        : Text::Align::Right;
        const double spacing = height * 1.667 * r.num(44, 1.0);
        const double total = height + spacing * static_cast<double>(lines.size() - 1);
        // Baseline of the first line from the attachment row (top, middle, bottom).
        const int row = (attach - 1) / 3;
        const double firstBelowTop = height;
        const double topOffset = row == 0 ? 0.0 : row == 1 ? total / 2.0 : total;
        const Vec2 dir = polar(1.0, rotation);
        const Vec2 up = perpendicular(dir);
        Vec2 baseline = r.point(10) + up * (topOffset - firstBelowTop);
        for (const QString& line : lines) {
            if (!line.trimmed().isEmpty()) {
                Text t;
                t.position = baseline;
                t.height = height;
                t.rotation = rotation;
                t.align = align;
                t.content = line.toStdString();
                add(layer, t, xf);
            }
            baseline = baseline - up * spacing;
        }
    }

    double dimensionBlockTextHeight(const QString& blockName) const
    {
        const auto it = blocks_.find(blockName);
        if (blockName.isEmpty() || it == blocks_.end())
            return 0.0;
        for (const Record& e : it->second.records) {
            if (e.type == QLatin1String("TEXT") || e.type == QLatin1String("MTEXT"))
                return e.num(40);
        }
        return 0.0;
    }

    void dimension(const Record& r, const Transform& xf, const std::string& layer)
    {
        // Sizes from the dimension style, else from the header variables.
        DimStyle st{dimScale_, dimText_, dimArrow_};
        const auto style = dimStyles_.find(r.str(3, QStringLiteral("STANDARD")).toUpper());
        if (style != dimStyles_.end()) {
            if (style->second.text > 0.0)
                st.text = style->second.text;
            if (style->second.arrow > 0.0)
                st.arrow = style->second.arrow;
            if (r.find(3))
                st.scale = style->second.scale;
        }
        Dimension d;
        d.textHeight = (st.text > 0.0 ? st.text : 2.5) * st.scale;
        d.arrowSize = (st.arrow > 0.0 ? st.arrow : st.text > 0.0 ? st.text : 2.5) * st.scale;
        // The style alone misses per-dimension overrides (kept in XDATA), so
        // trust the label actually drawn in the dimension's block when there is one.
        if (const double drawn = dimensionBlockTextHeight(r.str(2)); drawn > 0.0) {
            d.arrowSize *= drawn / d.textHeight;
            d.textHeight = drawn;
        }
        const QString text = decodeText(r.str(1));
        if (!text.isEmpty() && text != QLatin1String("<>"))
            d.text = text.toStdString();
        switch (r.integer(70) & 7) {
        case 0:
            d.kind = Dimension::Kind::Linear;
            d.p1 = r.point(13);
            d.p2 = r.point(14);
            d.location = r.point(10);
            d.angle = degToRad(r.num(50));
            break;
        case 1:
            d.kind = Dimension::Kind::Aligned;
            d.p1 = r.point(13);
            d.p2 = r.point(14);
            d.location = r.point(10);
            break;
        case 3:
            d.kind = Dimension::Kind::Diameter;
            d.p1 = midpoint(r.point(10), r.point(15));
            d.p2 = r.point(15);
            d.location = r.point(11);
            break;
        case 4:
            d.kind = Dimension::Kind::Radius;
            d.p1 = r.point(10);
            d.p2 = r.point(15);
            d.location = r.point(11);
            break;
        default:
            skip(QStringLiteral("DIMENSION angolare/coordinata"));
            return;
        }
        add(layer, d, xf);
    }

    void convert(const Record& r, const Transform& xf, const Transform& full, const std::string& layer)
    {
        const QString& type = r.type;
        if (type == QLatin1String("LINE")) {
            add(layer, Line{r.point(10), r.point(11)}, xf);
        } else if (type == QLatin1String("CIRCLE")) {
            if (r.num(40) > 0.0)
                add(layer, Circle{r.point(10), r.num(40)}, full);
        } else if (type == QLatin1String("ARC")) {
            if (r.num(40) > 0.0)
                add(layer, Arc{r.point(10), r.num(40), normalizeAngle(degToRad(r.num(50))), normalizeAngle(degToRad(r.num(51)))},
                    full);
        } else if (type == QLatin1String("LWPOLYLINE")) {
            Polyline pl;
            pl.closed = r.integer(70) & 1;
            for (const Group& g : r.groups) {
                if (g.code == 10)
                    pl.points.push_back({toDouble(g.value), 0.0});
                else if (g.code == 20 && !pl.points.empty())
                    pl.points.back().y = toDouble(g.value);
                else if (g.code == 42 && !pl.points.empty())
                    pl.setBulge(pl.points.size() - 1, toDouble(g.value));
            }
            if (pl.points.size() >= 2)
                add(layer, pl, full);
        } else if (type == QLatin1String("TEXT") || type == QLatin1String("ATTRIB")) {
            text(r, full, layer);
        } else if (type == QLatin1String("MTEXT")) {
            mtext(r, xf, layer);
        } else if (type == QLatin1String("DIMENSION")) {
            dimension(r, xf, layer);
        } else if (type == QLatin1String("SOLID") || type == QLatin1String("TRACE")) {
            // Corner order 1, 2, 4, 3 (the DXF "bow tie" order).
            Polyline pl{{r.point(10), r.point(11), r.point(13), r.point(12)}, true};
            if (almostEqual(r.point(12), r.point(13)))
                pl.points.pop_back();
            add(layer, pl, full);
        } else if (type == QLatin1String("LEADER")) {
            Polyline pl;
            for (const Group& g : r.groups) {
                if (g.code == 10)
                    pl.points.push_back({toDouble(g.value), 0.0});
                else if (g.code == 20 && !pl.points.empty())
                    pl.points.back().y = toDouble(g.value);
            }
            if (pl.points.size() >= 2)
                add(layer, pl, xf);
        } else if (type == QLatin1String("ELLIPSE")) {
            ellipse(r, xf, layer);
        } else if (type == QLatin1String("SPLINE")) {
            spline(r, xf, layer);
        } else if (type == QLatin1String("HATCH")) {
            hatch(r, full, layer);
        } else if (type == QLatin1String("POINT") || type == QLatin1String("ATTDEF") || type == QLatin1String("SEQEND")
                   || type == QLatin1String("VIEWPORT")) {
            // Nothing to draw.
        } else {
            skip(type);
        }
    }

    // HATCH: the groups repeat (one set per boundary edge and per pattern
    // line), so they are read in order rather than looked up by code.
    void hatch(const Record& r, const Transform& xf, const std::string& layer)
    {
        const auto& g = r.groups;
        size_t i = 0;
        auto at = [&](int code) { return i < g.size() && g[i].code == code; };
        auto seek = [&](int code) {
            while (i < g.size() && g[i].code != code)
                ++i;
            return i < g.size();
        };
        auto get = [&](int code, double fallback = 0.0) {
            if (!at(code))
                return fallback;
            return toDouble(g[i++].value);
        };
        auto getPoint = [&](int code) {
            const double x = get(code);
            return Vec2{x, get(code + 10)};
        };

        Hatch h;
        if (!seek(2)) {
            skip(QStringLiteral("HATCH"));
            return;
        }
        h.pattern = g[i++].value.trimmed().toUpper().toStdString();
        const bool solidFill = get(70) != 0.0;
        get(71);
        const int paths = static_cast<int>(get(91));
        for (int p = 0; p < paths; ++p) {
            if (!seek(92))
                break;
            const int flags = static_cast<int>(get(92));
            Polyline loop;
            loop.closed = true;
            if (flags & 2) {
                const bool bulges = get(72) != 0.0;
                get(73);
                const int count = static_cast<int>(get(93));
                for (int k = 0; k < count && at(10); ++k) {
                    loop.points.push_back(getPoint(10));
                    loop.bulges.push_back(bulges ? get(42) : 0.0);
                }
            } else {
                const int edges = static_cast<int>(get(93));
                for (int k = 0; k < edges && at(72); ++k)
                    hatchEdge(static_cast<int>(get(72)), at, get, getPoint, loop);
            }
            // Source objects of an associative hatch.
            const int sources = static_cast<int>(get(97));
            for (int k = 0; k < sources && at(330); ++k)
                ++i;
            // Drop a last point that repeats the first one.
            if (loop.points.size() > 2 && almostEqual(loop.points.front(), loop.points.back(), 1e-9)) {
                loop.points.pop_back();
                loop.bulges.resize(loop.points.size());
            }
            if (loop.points.size() >= 2)
                h.loops.push_back(std::move(loop));
        }
        if (h.loops.empty()) {
            skip(QStringLiteral("HATCH"));
            return;
        }
        if (!solidFill) {
            seek(52);
            const double angle = degToRad(get(52));
            double scale = get(41, 1.0);
            if (scale <= 0.0)
                scale = 1.0;
            // Skip "double" before the pattern lines.
            get(77);
            const int count = static_cast<int>(get(78));
            const Transform unrotate = Transform::rotation({0, 0}, -angle);
            for (int k = 0; k < count && at(53); ++k) {
                // In the file the lines are already scaled and rotated, and the
                // offset is a vector in the drawing: back to the definition.
                const double lineAngle = degToRad(get(53));
                HatchPatternLine l;
                l.angle = lineAngle - angle;
                const double bx = get(43), by = get(44);
                const double ox = get(45), oy = get(46);
                l.origin = unrotate.applyVector({bx, by}) / scale;
                l.offset = Transform::rotation({0, 0}, -lineAngle).applyVector({ox, oy}) / scale;
                const int dashes = static_cast<int>(get(79));
                for (int d = 0; d < dashes && at(49); ++d)
                    l.dashes.push_back(get(49) / scale);
                h.lines.push_back(std::move(l));
            }
            if (h.lines.empty()) {
                if (const auto* known = hatchPattern(h.pattern))
                    h.lines = *known;
            }
            h.scale = scale;
            h.angle = angle;
            if (h.lines.empty())
                h.pattern = "SOLID";
        } else {
            h.pattern = "SOLID";
        }
        add(layer, h, xf);
    }

    // One edge of a hatch boundary, appended to `loop` (its end point is the
    // start of the next edge).
    template <class At, class Get, class GetPoint>
    void hatchEdge(int type, At at, Get get, GetPoint getPoint, Polyline& loop)
    {
        auto addPoint = [&](Vec2 p, double bulge = 0.0) {
            if (!loop.points.empty() && almostEqual(loop.points.back(), p, 1e-9) && loop.bulges.back() == 0.0) {
                loop.bulges.back() = bulge;
                return;
            }
            loop.points.push_back(p);
            loop.bulges.push_back(bulge);
        };
        switch (type) {
        case 1: {
            const Vec2 a = getPoint(10);
            getPoint(11);
            addPoint(a);
            break;
        }
        case 2: {
            const Vec2 c = getPoint(10);
            const double radius = get(40);
            double start = degToRad(get(50));
            double end = degToRad(get(51));
            const bool ccw = get(73, 1.0) != 0.0;
            // Clockwise edges store the complements of their end and start angles.
            if (!ccw) {
                const double s0 = start;
                start = kTwoPi - end;
                end = kTwoPi - s0;
            }
            double sweep = ccw ? end - start : start - end;
            while (sweep <= 0.0)
                sweep += kTwoPi;
            while (sweep > kTwoPi + 1e-9)
                sweep -= kTwoPi;
            if (!ccw)
                sweep = -sweep;
            // A full circle is two half circles.
            if (std::abs(std::abs(sweep) - kTwoPi) < 1e-9) {
                addPoint(c + polar(radius, start), ccw ? 1.0 : -1.0);
                addPoint(c + polar(radius, start + sweep / 2.0), ccw ? 1.0 : -1.0);
            } else {
                addPoint(c + polar(radius, start), bulgeOfSweep(sweep));
            }
            break;
        }
        case 3: {
            const Vec2 c = getPoint(10);
            const Vec2 major = getPoint(11);
            const double ratio = get(40, 1.0);
            double start = degToRad(get(50));
            double end = degToRad(get(51));
            const bool ccw = get(73, 1.0) != 0.0;
            if (!ccw) {
                const double s0 = start;
                start = kTwoPi - end;
                end = kTwoPi - s0;
            }
            double sweep = ccw ? end - start : start - end;
            while (sweep <= 0.0)
                sweep += kTwoPi;
            if (!ccw)
                sweep = -sweep;
            const Vec2 minor = perpendicular(major) * ratio;
            const int steps = std::max(8, static_cast<int>(64.0 * std::abs(sweep) / kTwoPi));
            for (int k = 0; k < steps; ++k) {
                const double t = start + sweep * k / steps;
                addPoint(c + major * std::cos(t) + minor * std::sin(t));
            }
            break;
        }
        case 4: {
            const int degree = static_cast<int>(get(94, 3));
            get(73);
            get(74);
            const int knotCount = static_cast<int>(get(95));
            const int controlCount = static_cast<int>(get(96));
            std::vector<double> knots, weights;
            std::vector<Vec2> control;
            for (int k = 0; k < knotCount && at(40); ++k)
                knots.push_back(get(40));
            for (int k = 0; k < controlCount && at(10); ++k) {
                control.push_back(getPoint(10));
                if (at(42))
                    weights.push_back(get(42));
            }
            std::vector<Vec2> fit;
            const int fitCount = static_cast<int>(get(97));
            for (int k = 0; k < fitCount && at(11); ++k)
                fit.push_back(getPoint(11));
            if (at(12))
                getPoint(12);
            if (at(13))
                getPoint(13);
            std::vector<Vec2> pts;
            if (control.size() >= 2 && degree >= 1 && knots.size() == control.size() + static_cast<size_t>(degree) + 1)
                pts = evaluateSpline(control, knots, weights, degree);
            else
                pts = curveThrough(fit);
            for (size_t k = 0; k + 1 < pts.size(); ++k)
                addPoint(pts[k]);
            break;
        }
        default:
            break;
        }
    }

    void ellipse(const Record& r, const Transform& xf, const std::string& layer)
    {
        const Vec2 c = r.point(10);
        const Vec2 major = r.point(11);
        const Vec2 minor = perpendicular(major) * r.num(40, 1.0);
        double start = r.num(41);
        double end = r.num(42, kTwoPi);
        if (end <= start)
            end += kTwoPi;
        const bool full = std::abs(end - start - kTwoPi) < 1e-9;
        const int steps = std::max(8, static_cast<int>(64.0 * (end - start) / kTwoPi));
        Polyline pl;
        pl.closed = full;
        for (int k = 0; k <= steps - (full ? 1 : 0); ++k) {
            const double t = start + (end - start) * k / steps;
            pl.points.push_back(c + major * std::cos(t) + minor * std::sin(t));
        }
        add(layer, pl, r.num(230, 1.0) < 0.0 ? xf * Transform::mirror({0, 0}, {0, 1}) : xf);
    }

    void spline(const Record& r, const Transform& xf, const std::string& layer)
    {
        auto collect = [&](int code) {
            std::vector<Vec2> pts;
            for (const Group& g : r.groups) {
                if (g.code == code)
                    pts.push_back({toDouble(g.value), 0.0});
                else if (g.code == code + 10 && !pts.empty())
                    pts.back().y = toDouble(g.value);
            }
            return pts;
        };
        std::vector<double> knots, weights;
        for (const Group& g : r.groups) {
            if (g.code == 40)
                knots.push_back(toDouble(g.value));
            else if (g.code == 41)
                weights.push_back(toDouble(g.value));
        }
        const std::vector<Vec2> control = collect(10);
        const int degree = r.integer(71, 3);
        std::vector<Vec2> pts;
        if (control.size() >= 2 && degree >= 1 && knots.size() == control.size() + static_cast<size_t>(degree) + 1)
            pts = evaluateSpline(control, knots, weights, degree);
        else
            pts = curveThrough(collect(11));  // fit points, when there is no usable control polygon
        if (pts.size() < 2) {
            skip(QStringLiteral("SPLINE"));
            return;
        }
        add(layer, Polyline{pts, false}, xf);
    }

    // Smooth curve through `fit` (centripetal Catmull-Rom), close to the
    // interpolating spline AutoCAD builds from fit points.
    static std::vector<Vec2> curveThrough(const std::vector<Vec2>& fit)
    {
        if (fit.size() < 3)
            return fit;
        const size_t n = fit.size();
        auto at = [&](long i) {
            // Mirror the end points to give the first and last span a tangent.
            if (i < 0)
                return fit[0] * 2.0 - fit[1];
            if (i >= static_cast<long>(n))
                return fit[n - 1] * 2.0 - fit[n - 2];
            return fit[static_cast<size_t>(i)];
        };
        constexpr int kSteps = 16;
        std::vector<Vec2> out{fit[0]};
        for (size_t i = 0; i + 1 < n; ++i) {
            const Vec2 p0 = at(static_cast<long>(i) - 1), p1 = fit[i], p2 = fit[i + 1], p3 = at(static_cast<long>(i) + 2);
            auto knot = [](Vec2 a, Vec2 b) { return std::max(std::sqrt(distance(a, b)), 1e-9); };
            const double t0 = 0.0, t1 = t0 + knot(p0, p1), t2 = t1 + knot(p1, p2), t3 = t2 + knot(p2, p3);
            for (int k = 1; k <= kSteps; ++k) {
                const double t = t1 + (t2 - t1) * k / kSteps;
                const Vec2 a1 = p0 * ((t1 - t) / (t1 - t0)) + p1 * ((t - t0) / (t1 - t0));
                const Vec2 a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
                const Vec2 a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
                const Vec2 b1 = a1 * ((t2 - t) / (t2 - t0)) + a2 * ((t - t0) / (t2 - t0));
                const Vec2 b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
                out.push_back(b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1)));
            }
        }
        return out;
    }

    // Points along a (rational) B-spline, by de Boor's algorithm.
    static std::vector<Vec2> evaluateSpline(const std::vector<Vec2>& control, const std::vector<double>& knots,
                                            const std::vector<double>& weights, int degree)
    {
        const size_t n = control.size();
        const size_t p = static_cast<size_t>(degree);
        const double t0 = knots[p];
        const double t1 = knots[n];
        const int steps = static_cast<int>(std::clamp<size_t>(n * 12, 24, 400));
        std::vector<Vec2> out;
        for (int k = 0; k <= steps; ++k) {
            const double t = k == steps ? t1 : t0 + (t1 - t0) * k / steps;
            // Knot span containing t.
            size_t span = p;
            while (span + 1 < n && t >= knots[span + 1])
                ++span;
            std::vector<Vec2> d(p + 1);
            std::vector<double> w(p + 1);
            for (size_t j = 0; j <= p; ++j) {
                const size_t idx = span - p + j;
                w[j] = idx < weights.size() && weights[idx] > 0.0 ? weights[idx] : 1.0;
                d[j] = control[idx] * w[j];
            }
            for (size_t level = 1; level <= p; ++level) {
                for (size_t j = p; j >= level; --j) {
                    const size_t i = span - p + j;
                    const double den = knots[i + p + 1 - level] - knots[i];
                    const double a = den > 0.0 ? (t - knots[i]) / den : 0.0;
                    d[j] = d[j - 1] * (1.0 - a) + d[j] * a;
                    w[j] = w[j - 1] * (1.0 - a) + w[j] * a;
                }
            }
            out.push_back(d[p] * (1.0 / w[p]));
        }
        return out;
    }

    Document& doc_;
    std::map<QString, BlockRecords> blocks_;
    // Named blocks already made, and those being made (to stop a block
    // that contains itself).
    std::map<QString, std::shared_ptr<const Block>> defined_;
    std::set<QString> defining_;
    // Where the objects go while a block is made; null for the drawing.
    std::vector<BlockItem>* target_ = nullptr;
    // In a block, BYBLOCK stays BYBLOCK; this says so for the colour.
    bool colorByBlock_ = false;
    std::map<QString, DimStyle> dimStyles_;
    std::map<QString, int> skipped_;
    int imported_ = 0;
    double dimScale_ = 1.0;
    double dimText_ = 0.0;
    double dimArrow_ = 0.0;
};

// ------------------------------------------------------------------ writing

class DxfWriter {
public:
    void group(int code, const QString& value)
    {
        out_ += QString::number(code).rightJustified(3, QLatin1Char(' ')).toLatin1();
        out_ += "\r\n";
        out_ += value.toLatin1();
        out_ += "\r\n";
    }
    void group(int code, const char* value) { group(code, QString::fromLatin1(value)); }
    void group(int code, double value) { group(code, number(value)); }
    void group(int code, int value) { group(code, QString::number(value)); }
    void point(int code, Vec2 p)
    {
        group(code, p.x);
        group(code + 10, p.y);
        group(code + 20, 0.0);
    }

    static QString number(double v)
    {
        if (std::abs(v) < 1e-12)
            v = 0.0;
        QString s = QString::number(v, 'f', 10);
        while (s.contains(QLatin1Char('.')) && (s.endsWith(QLatin1Char('0'))))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s += QLatin1Char('0');
        return s;
    }

    // R12 files are 8-bit in the Windows code page (declared in the header):
    // Ø, ° and ± use the classic %% codes, Latin-1 letters go as they are and
    // anything else as \U+XXXX.
    static QString encode(const std::string& utf8)
    {
        const QString s = QString::fromStdString(utf8);
        QString out;
        for (QChar c : s) {
            if (c == QChar(0x00D8))
                out += QLatin1String("%%c");
            else if (c == QChar(0x00B0))
                out += QLatin1String("%%d");
            else if (c == QChar(0x00B1))
                out += QLatin1String("%%p");
            else if (c.unicode() < 256)
                out += c;
            else
                out += QStringLiteral("\\U+") + QString::number(c.unicode(), 16).toUpper().rightJustified(4, QLatin1Char('0'));
        }
        return out;
    }

    void line(const std::string& layer, Vec2 a, Vec2 b)
    {
        group(0, "LINE");
        this->layer(layer);
        point(10, a);
        point(11, b);
    }

    void text(const std::string& layer, const Text& t)
    {
        group(0, "TEXT");
        this->layer(layer);
        point(10, t.position);
        group(40, t.height);
        group(1, encode(t.content));
        if (std::abs(t.rotation) > 1e-12)
            group(50, radToDeg(t.rotation));
        if (t.align != Text::Align::Left) {
            group(72, t.align == Text::Align::Center ? 1 : 2);
            point(11, t.position);
        }
    }

    void solid(const std::string& layer, const std::array<Vec2, 3>& tri)
    {
        group(0, "SOLID");
        this->layer(layer);
        point(10, tri[0]);
        point(11, tri[1]);
        point(12, tri[2]);
        point(13, tri[2]);
    }

    // The layer of an entity, then its own colour and linetype (R12 has no lineweights).
    void layer(const std::string& name)
    {
        group(8, layerName(name));
        if (linetypeKey(style_.linetype) != kByLayer)
            group(6, encode(style_.linetype));
        if (colorByBlock_)
            group(62, 0);
        else if (style_.color)
            group(62, colorToAci(*style_.color));
    }

    // An object as DXF has it: walls, slabs, roofs and stairs go out as what
    // they draw in plan. `faces` are those of a wall joined to others.
    void object(const std::string& layer, const Shape& s, const WallFaces* faces, int& dimCount, QByteArray& blocks)
    {
        if (const auto* slab = std::get_if<Slab>(&s)) {
            for (const Polyline& loop : slab->loops)
                shape(layer, loop, dimCount, blocks);
        } else if (const auto* roof = std::get_if<Roof>(&s)) {
            for (const Shape& part : roofPlan(*roof))
                shape(layer, part, dimCount, blocks);
        } else if (const auto* stair = std::get_if<Stair>(&s)) {
            for (const Shape& part : stairPlan(*stair))
                shape(layer, part, dimCount, blocks);
        } else if (const auto* wall = std::get_if<Wall>(&s)) {
            for (const Shape& part : faces ? wallPlan(*wall, *faces) : wallPlan(*wall))
                shape(layer, part, dimCount, blocks);
        } else {
            shape(layer, s, dimCount, blocks);
        }
    }

    void shape(const std::string& layer, const Shape& s, int& dimCount, QByteArray& blocks);
    void hatch(const std::string& layer, const Hatch& h, QByteArray& blocks);

    int hatchCount_ = 0;
    EntityStyle style_;
    bool colorByBlock_ = false;

    static QString layerName(const std::string& name) { return encode(name); }

    QByteArray out_;
};

void DxfWriter::shape(const std::string& layer, const Shape& s, int& dimCount, QByteArray& blocks)
{
    if (const auto* l = std::get_if<Line>(&s)) {
        line(layer, l->a, l->b);
    } else if (const auto* c = std::get_if<Circle>(&s)) {
        group(0, "CIRCLE");
        this->layer(layer);
        point(10, c->center);
        group(40, c->radius);
    } else if (const auto* a = std::get_if<Arc>(&s)) {
        group(0, "ARC");
        this->layer(layer);
        point(10, a->center);
        group(40, a->radius);
        group(50, radToDeg(normalizeAngle(a->startAngle)));
        group(51, radToDeg(normalizeAngle(a->endAngle)));
    } else if (const auto* pl = std::get_if<Polyline>(&s)) {
        group(0, "POLYLINE");
        this->layer(layer);
        group(66, 1);
        point(10, {0.0, 0.0});
        group(70, pl->closed ? 1 : 0);
        for (size_t i = 0; i < pl->points.size(); ++i) {
            group(0, "VERTEX");
            this->layer(layer);
            point(10, pl->points[i]);
            if (std::abs(pl->bulge(i)) > 1e-12)
                group(42, pl->bulge(i));
        }
        group(0, "SEQEND");
        this->layer(layer);
    } else if (const auto* t = std::get_if<Text>(&s)) {
        text(layer, *t);
    } else if (const auto* h = std::get_if<Hatch>(&s)) {
        hatch(layer, *h, blocks);
    } else if (const auto* ref = std::get_if<Insert>(&s)) {
        if (!ref->block)
            return;
        group(0, "INSERT");
        this->layer(layer);
        group(2, encode(ref->block->name));
        point(10, ref->position);
        if (ref->scaleX != 1.0)
            group(41, ref->scaleX);
        if (ref->scaleY != 1.0)
            group(42, ref->scaleY);
        if (std::abs(ref->rotation) > 1e-12)
            group(50, radToDeg(normalizeAngle(ref->rotation)));
    } else if (const auto* d = std::get_if<Dimension>(&s)) {
        // The drawn geometry goes into an anonymous block, as AutoCAD does.
        const QString name = QStringLiteral("*D%1").arg(++dimCount);
        DxfWriter block;
        block.group(0, "BLOCK");
        block.group(8, "0");
        block.group(2, name);
        block.group(70, 1);
        block.point(10, {0.0, 0.0});
        block.group(3, name);
        const DimensionGeometry g = dimensionGeometry(*d);
        for (const Line& l : g.lines)
            block.line("0", l.a, l.b);
        for (const auto& arrow : g.arrows)
            block.solid("0", arrow);
        block.text("0", g.label);
        block.group(0, "ENDBLK");
        block.group(8, "0");
        blocks += block.out_;

        group(0, "DIMENSION");
        this->layer(layer);
        group(2, name);
        group(3, "STANDARD");
        switch (d->kind) {
        case Dimension::Kind::Linear:
        case Dimension::Kind::Aligned: {
            const bool aligned = d->kind == Dimension::Kind::Aligned;
            const Vec2 dir = aligned ? normalized(d->p2 - d->p1) : polar(1.0, d->angle);
            const Vec2 n = perpendicular(dir);
            // Definition point: where the dimension line meets the second extension line.
            point(10, d->p2 + n * dot(d->location - d->p2, n));
            point(11, g.label.position);
            group(70, aligned ? 1 : 0);
            if (!d->text.empty())
                group(1, encode(d->text));
            point(13, d->p1);
            point(14, d->p2);
            if (!aligned)
                group(50, radToDeg(d->angle));
            break;
        }
        case Dimension::Kind::Radius:
        case Dimension::Kind::Diameter: {
            const bool diameter = d->kind == Dimension::Kind::Diameter;
            point(10, diameter ? d->p1 * 2.0 - d->p2 : d->p1);
            point(11, d->location);
            group(70, diameter ? 3 : 4);
            if (!d->text.empty())
                group(1, encode(d->text));
            point(15, d->p2);
            group(40, 0.0);
            break;
        }
        }
    }
}

// R12 has no HATCH entity: AutoCAD R12 itself kept a hatch as an anonymous
// block of lines, inserted on the hatch's layer. A solid fill becomes SOLIDs.
void DxfWriter::hatch(const std::string& layer, const Hatch& h, QByteArray& blocks)
{
    const QString name = QStringLiteral("*X%1").arg(++hatchCount_);
    DxfWriter block;
    block.group(0, "BLOCK");
    block.group(8, "0");
    block.group(2, name);
    block.group(70, 1);
    block.point(10, {0.0, 0.0});
    block.group(3, name);
    bool dense = false;
    const auto lines = h.solid() ? std::vector<Line>{}
                                 : hatchLines(h, BBox::fromCorners({-1e300, -1e300}, {1e300, 1e300}), 500000, &dense);
    if (h.solid()) {
        for (const auto& q : hatchTrapezoids(h)) {
            block.group(0, "SOLID");
            block.group(8, "0");
            // SOLID corners go in "bow tie" order: 1, 2, 4, 3.
            block.point(10, q[0]);
            block.point(11, q[1]);
            block.point(12, q[3]);
            block.point(13, q[2]);
        }
    } else if (dense) {
        // Far too many lines for a file: keep the outline only.
        int unused = 0;
        QByteArray none;
        for (const Polyline& loop : h.loops)
            block.shape("0", loop, unused, none);
    } else {
        for (const Line& l : lines) {
            if (l.a == l.b) {
                block.group(0, "POINT");
                block.group(8, "0");
                block.point(10, l.a);
            } else {
                block.line("0", l.a, l.b);
            }
        }
    }
    block.group(0, "ENDBLK");
    block.group(8, "0");
    blocks += block.out_;

    group(0, "INSERT");
    this->layer(layer);
    group(2, name);
    point(10, {0.0, 0.0});
}

} // namespace

bool fromDxf(const QByteArray& data, Document& doc, QString* message)
{
    Document loaded;
    DxfImporter importer(loaded);
    QString error;
    if (!importer.parse(data, &error)) {
        if (message)
            *message = error;
        return false;
    }
    loaded.clearHistory();
    loaded.setModified(false);
    doc = std::move(loaded);

    if (message) {
        QStringList left;
        for (const auto& [type, count] : importer.skipped())
            left << QStringLiteral("%1 %2").arg(count).arg(type);
        *message = left.isEmpty() ? QString()
                                  : QStringLiteral("Importati %1 oggetti; non importati o approssimati: %2.")
                                        .arg(importer.imported())
                                        .arg(left.join(QStringLiteral(", ")));
    }
    return true;
}

QByteArray toDxf(const Document& doc)
{
    DxfWriter w;
    const BBox ext = doc.extents();
    w.group(0, "SECTION");
    w.group(2, "HEADER");
    w.group(9, "$ACADVER");
    w.group(1, "AC1009");
    w.group(9, "$DWGCODEPAGE");
    w.group(3, "ANSI_1252");
    w.group(9, "$EXTMIN");
    w.point(10, ext.valid() ? ext.min : Vec2{0, 0});
    w.group(9, "$EXTMAX");
    w.point(10, ext.valid() ? ext.max : Vec2{0, 0});
    w.group(9, "$LTSCALE");
    w.group(40, doc.linetypeScale());
    w.group(0, "ENDSEC");

    w.group(0, "SECTION");
    w.group(2, "TABLES");
    w.group(0, "TABLE");
    w.group(2, "LTYPE");
    w.group(70, static_cast<int>(doc.linetypes().size()));
    for (const Linetype& t : doc.linetypes()) {
        w.group(0, "LTYPE");
        w.group(2, DxfWriter::encode(t.name));
        w.group(70, 0);
        w.group(3, DxfWriter::encode(t.description));
        w.group(72, 65);
        w.group(73, static_cast<int>(t.pattern.size()));
        w.group(40, t.patternLength());
        for (double d : t.pattern)
            w.group(49, d);
    }
    w.group(0, "ENDTAB");
    w.group(0, "TABLE");
    w.group(2, "LAYER");
    w.group(70, static_cast<int>(doc.layers().size()));
    for (const Layer& l : doc.layers()) {
        w.group(0, "LAYER");
        w.group(2, DxfWriter::layerName(l.name));
        w.group(70, l.locked ? 4 : 0);
        const int aci = colorToAci(l.color);
        w.group(62, l.visible ? aci : -aci);
        w.group(6, DxfWriter::encode(l.linetype));
    }
    w.group(0, "ENDTAB");
    w.group(0, "TABLE");
    w.group(2, "DIMSTYLE");
    w.group(70, 1);
    w.group(0, "DIMSTYLE");
    w.group(2, "STANDARD");
    w.group(70, 0);
    w.group(0, "ENDTAB");
    w.group(0, "TABLE");
    w.group(2, "STYLE");
    w.group(70, 1);
    w.group(0, "STYLE");
    w.group(2, "STANDARD");
    w.group(70, 0);
    w.group(40, 0.0);
    w.group(41, 1.0);
    w.group(50, 0.0);
    w.group(71, 0);
    w.group(42, 2.5);
    w.group(3, "txt");
    w.group(4, "");
    w.group(0, "ENDTAB");
    w.group(0, "ENDSEC");

    QByteArray blocks;
    int dimCount = 0;
    DxfWriter entities;
    // DXF has no walls, slabs, roofs or stairs: they go out as what they draw in plan.
    const std::map<EntityId, WallFaces> walls = joinedWalls(doc);
    // Only the current story: the others would lie on top of it.
    for (const auto& [id, e] : doc.entities()) {
        if (e.story != doc.currentStory())
            continue;
        entities.style_ = styleOf(e);
        const auto faces = walls.find(id);
        entities.object(e.layer, e.shape, faces == walls.end() ? nullptr : &faces->second, dimCount, blocks);
    }
    // The blocks of the drawing, objects in block coordinates.
    int hatchCount = entities.hatchCount_;
    for (const auto& [key, b] : doc.blocks()) {
        DxfWriter block;
        block.hatchCount_ = hatchCount;
        block.group(0, "BLOCK");
        block.group(8, "0");
        block.group(2, DxfWriter::encode(b->name));
        block.group(70, 0);
        block.point(10, b->base);
        block.group(3, DxfWriter::encode(b->name));
        for (const BlockItem& item : b->items) {
            block.style_ = {item.color, item.linetype, item.lineweight};
            block.colorByBlock_ = item.colorByBlock;
            block.object(item.layer, item.shape, nullptr, dimCount, blocks);
        }
        block.group(0, "ENDBLK");
        block.group(8, "0");
        blocks += block.out_;
        hatchCount = block.hatchCount_;
    }

    w.group(0, "SECTION");
    w.group(2, "BLOCKS");
    w.out_ += blocks;
    w.group(0, "ENDSEC");
    w.group(0, "SECTION");
    w.group(2, "ENTITIES");
    w.out_ += entities.out_;
    w.group(0, "ENDSEC");
    w.group(0, "EOF");
    return w.out_;
}

bool loadDxf(Document& doc, const QString& path, QString* message)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (message)
            *message = file.errorString();
        return false;
    }
    return fromDxf(file.readAll(), doc, message);
}

bool saveDxf(const Document& doc, const QString& path, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(toDxf(doc));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace cad::io
