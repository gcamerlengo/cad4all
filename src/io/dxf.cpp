// SPDX-License-Identifier: GPL-3.0-or-later
#include "dxf.h"

#include "building.h"
#include "hatch.h"
#include "nativeformat.h"

#include <QFile>
#include <QJsonDocument>
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
            if (r.find(420)) {
                const unsigned rgb = static_cast<unsigned>(r.integer(420));
                l.color = Color{static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8),
                                static_cast<std::uint8_t>(rgb)};
            }
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
                    if (records[j].integer(70) & 1)
                        continue; // invisible attribute
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
        // A wall, slab, roof or stair written by CAD4All: the object itself,
        // moved and turned as the reference was (another program may have
        // moved it), instead of the drawing of its plan.
        if (const auto object = cad4allObject(r)) {
            const Transform place = Transform::translation(r.point(10)) * Transform::rotation({0, 0}, degToRad(r.num(50)));
            add(layer, *object, xf * place);
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

    // The object CAD4All keeps in the XDATA of a reference, if there is one.
    std::optional<Shape> cad4allObject(const Record& r) const
    {
        QByteArray json;
        bool ours = false;
        for (const Group& g : r.groups) {
            if (g.code == 1001)
                ours = g.value.trimmed() == QLatin1String("CAD4ALL");
            else if (ours && g.code == 1000)
                json += g.value.toLatin1();
        }
        if (json.isEmpty())
            return std::nullopt;
        const QJsonDocument parsed = QJsonDocument::fromJson(json);
        if (!parsed.isObject())
            return std::nullopt;
        return shapeFromNativeJson(parsed.object(), doc_);
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
        MText m;
        m.content = mtextLines(raw).join(QLatin1Char('\n')).toStdString();
        m.position = r.point(10);
        m.height = r.num(40, 2.5);
        m.width = std::max(0.0, r.num(41));
        m.rotation = degToRad(r.num(50));
        if (r.find(11))
            m.rotation = angleOf(r.point(11));
        m.lineSpacing = r.num(44, 1.0) > 0.0 ? r.num(44, 1.0) : 1.0;
        // Attachment 1..9: top, middle, bottom rows of left, centre, right.
        const int attach = std::clamp(r.integer(71, 1), 1, 9);
        m.align = (attach - 1) % 3 == 0 ? Text::Align::Left : (attach - 1) % 3 == 1 ? Text::Align::Center : Text::Align::Right;
        m.vertical = (attach - 1) / 3 == 0 ? MText::Vertical::Top : (attach - 1) / 3 == 1 ? MText::Vertical::Middle : MText::Vertical::Bottom;
        if (m.height <= 0.0 || QString::fromStdString(m.content).trimmed().isEmpty())
            return;
        add(layer, m, xf);
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
            // Angular and ordinate dimensions: CAD4All has no such object, so
            // keep the lines, arrows and label the dimension's block draws.
            explodeDimensionBlock(r, xf, layer);
            return;
        }
        add(layer, d, xf);
    }

    void explodeDimensionBlock(const Record& r, const Transform& xf, const std::string& layer)
    {
        const auto it = blocks_.find(r.str(2));
        if (r.str(2).isEmpty() || it == blocks_.end()) {
            skip(QStringLiteral("DIMENSION angolare/coordinata"));
            return;
        }
        const EntityStyle outer = blockStyle_;
        blockStyle_ = style_;
        const bool outerExploding = exploding_;
        exploding_ = true;
        convertList(it->second.records, xf * Transform::translation(it->second.base * -1.0), layer, 1, false);
        exploding_ = outerExploding;
        blockStyle_ = outer;
    }

    void convert(const Record& r, const Transform& xf, const Transform& full, const std::string& layer)
    {
        const QString& type = r.type;
        if (type == QLatin1String("LINE")) {
            add(layer, Line{r.point(10), r.point(11)}, xf);
        } else if (type == QLatin1String("POINT")) {
            add(layer, Point{r.point(10)}, xf);
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
        } else if (type == QLatin1String("ATTDEF") || type == QLatin1String("SEQEND")
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

// What the writers of one file share: the version, the handles (DXF 2000
// gives every object one) and the block records the BLOCKS section needs.
struct DxfContext {
    explicit DxfContext(DxfVersion v) : modern(v != DxfVersion::R12) {}

    bool modern;
    unsigned next = 0x30;
    std::vector<std::pair<QString, unsigned>> blockRecords;  // name, record handle
    int dimCount = 0;
    int hatchCount = 0;
    int objectCount = 0;
    QByteArray blocks;

    unsigned handle() { return next++; }
    unsigned blockRecord(const QString& name)
    {
        const unsigned h = handle();
        blockRecords.push_back({name, h});
        return h;
    }
};

class DxfWriter {
public:
    DxfWriter(DxfContext& ctx, unsigned owner) : ctx_(ctx), owner_(owner) {}

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
    void hex(int code, unsigned h) { group(code, QString::number(h, 16).toUpper()); }
    void point(int code, Vec2 p)
    {
        group(code, p.x);
        group(code + 10, p.y);
        group(code + 20, 0.0);
    }
    // A subclass marker, which only DXF 2000 has.
    void sub(const char* name)
    {
        if (ctx_.modern)
            group(100, name);
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

    // Both versions are 8-bit in the Windows code page (declared in the
    // header): Ø, ° and ± use the classic %% codes, Latin-1 letters go as
    // they are and anything else as \U+XXXX.
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

    // The start of an entity: its type, then (DXF 2000) its handle and owner,
    // then its layer and style.
    void begin(const char* type, const std::string& layer)
    {
        group(0, type);
        if (ctx_.modern) {
            hex(5, ctx_.handle());
            hex(330, owner_);
            group(100, "AcDbEntity");
        }
        this->layer(layer);
    }

    void line(const std::string& layer, Vec2 a, Vec2 b)
    {
        begin("LINE", layer);
        sub("AcDbLine");
        point(10, a);
        point(11, b);
    }

    void text(const std::string& layer, const Text& t)
    {
        begin("TEXT", layer);
        sub("AcDbText");
        point(10, t.position);
        group(40, t.height);
        group(1, encode(t.content));
        if (std::abs(t.rotation) > 1e-12)
            group(50, radToDeg(t.rotation));
        if (t.align != Text::Align::Left) {
            group(72, t.align == Text::Align::Center ? 1 : 2);
            point(11, t.position);
        }
        sub("AcDbText");
    }

    void mtext(const std::string& layer, const MText& m)
    {
        begin("MTEXT", layer);
        sub("AcDbMText");
        point(10, m.position);
        // Extrusion and direction before the sizes, in the order AutoCAD
        // writes them: LibreDWG reads the groups in that order.
        group(210, 0.0);
        group(220, 0.0);
        group(230, 1.0);
        point(11, polar(1.0, m.rotation));
        group(40, m.height);
        group(41, m.width);
        const int column = m.align == Text::Align::Left ? 1 : m.align == Text::Align::Center ? 2 : 3;
        const int row = m.vertical == MText::Vertical::Top ? 0 : m.vertical == MText::Vertical::Middle ? 1 : 2;
        group(71, row * 3 + column);
        group(72, 1);
        QString content = QString::fromStdString(m.content);
        content.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
        content.replace(QLatin1Char('{'), QLatin1String("\\{"));
        content.replace(QLatin1Char('}'), QLatin1String("\\}"));
        content.replace(QLatin1Char('\n'), QLatin1String("\\P"));
        content = encode(content.toStdString());
        // The text in pieces of 250 characters: 3 for all but the last, then 1.
        while (content.size() > 250) {
            group(3, content.left(250));
            content.remove(0, 250);
        }
        group(1, content);
        group(7, "STANDARD");
        if (m.lineSpacing != 1.0) {
            group(73, 1);
            group(44, m.lineSpacing);
        }
    }

    void solid(const std::string& layer, const std::array<Vec2, 4>& corners)
    {
        begin("SOLID", layer);
        sub("AcDbTrace");
        point(10, corners[0]);
        point(11, corners[1]);
        point(12, corners[2]);
        point(13, corners[3]);
    }
    void solid(const std::string& layer, const std::array<Vec2, 3>& tri) { solid(layer, {tri[0], tri[1], tri[2], tri[2]}); }

    // The layer of an entity, then its own colour, linetype and (DXF 2000)
    // lineweight.
    void layer(const std::string& name)
    {
        group(8, layerName(name));
        if (linetypeKey(style_.linetype) != kByLayer)
            group(6, encode(style_.linetype));
        if (colorByBlock_) {
            group(62, 0);
        } else if (style_.color) {
            const auto aci = exactAci(*style_.color);
            group(62, aci ? *aci : colorToAci(*style_.color));
            if (ctx_.modern && !aci)
                group(420, static_cast<int>((style_.color->r << 16) | (style_.color->g << 8) | style_.color->b));
        }
        if (ctx_.modern && style_.lineweight != kLineweightByLayer)
            group(370, style_.lineweight);
    }

    // An object as DXF has it: walls, slabs, roofs and stairs go out as what
    // they draw in plan. `faces` are those of a wall joined to others. In DXF
    // 2000 that drawing is a block, inserted with the object itself in XDATA,
    // so CAD4All reads it back as it was and other programs see the plan.
    void object(const std::string& layer, const Shape& s, const WallFaces* faces)
    {
        std::vector<Shape> plan;
        if (const auto* slab = std::get_if<Slab>(&s)) {
            plan.assign(slab->loops.begin(), slab->loops.end());
        } else if (const auto* roof = std::get_if<Roof>(&s)) {
            plan = roofPlan(*roof);
        } else if (const auto* stair = std::get_if<Stair>(&s)) {
            plan = stairPlan(*stair);
        } else if (const auto* wall = std::get_if<Wall>(&s)) {
            plan = faces ? wallPlan(*wall, *faces) : wallPlan(*wall);
        } else {
            shape(layer, s);
            return;
        }
        if (!ctx_.modern) {
            for (const Shape& part : plan)
                shape(layer, part);
            return;
        }
        const QString name = QStringLiteral("*U%1").arg(++ctx_.objectCount);
        DxfWriter block(ctx_, ctx_.blockRecord(name));
        block.blockBegin(name, {0.0, 0.0}, 1);
        // The parts take the layer and style of the reference.
        block.colorByBlock_ = true;
        block.style_.linetype = "BYBLOCK";
        block.style_.lineweight = kLineweightByBlock;
        for (const Shape& part : plan)
            block.shape("0", part);
        block.blockEnd();
        ctx_.blocks += block.out_;

        begin("INSERT", layer);
        sub("AcDbBlockReference");
        group(2, name);
        point(10, {0.0, 0.0});
        xdata(QJsonDocument(shapeToNativeJson(s)).toJson(QJsonDocument::Compact));
    }

    // CAD4All's own data, readable only by CAD4All: JSON in pieces of 200
    // characters (XDATA strings stop at 255, and LibreDWG drops longer ones).
    void xdata(const QByteArray& json)
    {
        group(1001, kAppName);
        for (qsizetype i = 0; i < json.size(); i += 200)
            group(1000, QString::fromLatin1(json.mid(i, 200)));
    }

    void blockBegin(const QString& name, Vec2 base, int flags)
    {
        group(0, "BLOCK");
        if (ctx_.modern) {
            hex(5, ctx_.handle());
            hex(330, owner_);
            group(100, "AcDbEntity");
        }
        group(8, "0");
        sub("AcDbBlockBegin");
        group(2, name);
        group(70, flags);
        point(10, base);
        group(3, name);
        if (ctx_.modern)
            group(1, "");
    }
    void blockEnd()
    {
        group(0, "ENDBLK");
        if (ctx_.modern) {
            hex(5, ctx_.handle());
            hex(330, owner_);
            group(100, "AcDbEntity");
        }
        group(8, "0");
        sub("AcDbBlockEnd");
    }

    void shape(const std::string& layer, const Shape& s);
    void polyline(const std::string& layer, const Polyline& pl);
    void hatch(const std::string& layer, const Hatch& h);
    void hatchAsBlock(const std::string& layer, const Hatch& h);
    void dimension(const std::string& layer, const Dimension& d);

    static constexpr const char* kAppName = "CAD4ALL";
    static QString layerName(const std::string& name) { return encode(name); }

    DxfContext& ctx_;
    unsigned owner_;
    EntityStyle style_;
    bool colorByBlock_ = false;
    QByteArray out_;
};

void DxfWriter::shape(const std::string& layer, const Shape& s)
{
    if (const auto* l = std::get_if<Line>(&s)) {
        line(layer, l->a, l->b);
    } else if (const auto* p = std::get_if<Point>(&s)) {
        begin("POINT", layer);
        sub("AcDbPoint");
        point(10, p->position);
    } else if (const auto* c = std::get_if<Circle>(&s)) {
        begin("CIRCLE", layer);
        sub("AcDbCircle");
        point(10, c->center);
        group(40, c->radius);
    } else if (const auto* a = std::get_if<Arc>(&s)) {
        begin("ARC", layer);
        sub("AcDbCircle");
        point(10, a->center);
        group(40, a->radius);
        sub("AcDbArc");
        group(50, radToDeg(normalizeAngle(a->startAngle)));
        group(51, radToDeg(normalizeAngle(a->endAngle)));
    } else if (const auto* pl = std::get_if<Polyline>(&s)) {
        polyline(layer, *pl);
    } else if (const auto* t = std::get_if<Text>(&s)) {
        text(layer, *t);
    } else if (const auto* m = std::get_if<MText>(&s)) {
        if (ctx_.modern) {
            mtext(layer, *m);
        } else {
            // R12 has no MTEXT: one TEXT per line.
            for (const Text& line : textLines(*m))
                text(layer, line);
        }
    } else if (const auto* h = std::get_if<Hatch>(&s)) {
        if (ctx_.modern)
            hatch(layer, *h);
        else
            hatchAsBlock(layer, *h);
    } else if (const auto* ref = std::get_if<Insert>(&s)) {
        if (!ref->block)
            return;
        begin("INSERT", layer);
        sub("AcDbBlockReference");
        group(2, encode(ref->block->name));
        point(10, ref->position);
        if (ref->scaleX != 1.0)
            group(41, ref->scaleX);
        if (ref->scaleY != 1.0)
            group(42, ref->scaleY);
        if (std::abs(ref->rotation) > 1e-12)
            group(50, radToDeg(normalizeAngle(ref->rotation)));
    } else if (const auto* d = std::get_if<Dimension>(&s)) {
        dimension(layer, *d);
    } else {
        object(layer, s, nullptr);
    }
}

void DxfWriter::polyline(const std::string& layer, const Polyline& pl)
{
    if (ctx_.modern) {
        begin("LWPOLYLINE", layer);
        sub("AcDbPolyline");
        group(90, static_cast<int>(pl.points.size()));
        group(70, pl.closed ? 1 : 0);
        for (size_t i = 0; i < pl.points.size(); ++i) {
            group(10, pl.points[i].x);
            group(20, pl.points[i].y);
            if (std::abs(pl.bulge(i)) > 1e-12)
                group(42, pl.bulge(i));
        }
        return;
    }
    begin("POLYLINE", layer);
    group(66, 1);
    point(10, {0.0, 0.0});
    group(70, pl.closed ? 1 : 0);
    for (size_t i = 0; i < pl.points.size(); ++i) {
        begin("VERTEX", layer);
        point(10, pl.points[i]);
        if (std::abs(pl.bulge(i)) > 1e-12)
            group(42, pl.bulge(i));
    }
    begin("SEQEND", layer);
}

void DxfWriter::dimension(const std::string& layer, const Dimension& d)
{
    // The drawn geometry goes into an anonymous block, as AutoCAD does.
    const QString name = QStringLiteral("*D%1").arg(++ctx_.dimCount);
    const DimensionGeometry g = dimensionGeometry(d);
    {
        DxfWriter block(ctx_, ctx_.modern ? ctx_.blockRecord(name) : 0);
        block.blockBegin(name, {0.0, 0.0}, 1);
        block.colorByBlock_ = true;
        for (const Line& l : g.lines)
            block.line("0", l.a, l.b);
        for (const auto& arrow : g.arrows)
            block.solid("0", arrow);
        block.text("0", g.label);
        block.blockEnd();
        ctx_.blocks += block.out_;
    }

    begin("DIMENSION", layer);
    sub("AcDbDimension");
    group(2, name);
    switch (d.kind) {
    case Dimension::Kind::Linear:
    case Dimension::Kind::Aligned: {
        const bool aligned = d.kind == Dimension::Kind::Aligned;
        const Vec2 dir = aligned ? normalized(d.p2 - d.p1) : polar(1.0, d.angle);
        const Vec2 n = perpendicular(dir);
        // Definition point: where the dimension line meets the second extension line.
        point(10, d.p2 + n * dot(d.location - d.p2, n));
        point(11, g.label.position);
        // 32: the block is this dimension's own (always set by AutoCAD).
        group(70, (aligned ? 1 : 0) | (ctx_.modern ? 32 : 0));
        if (!d.text.empty())
            group(1, encode(d.text));
        group(3, "STANDARD");
        sub("AcDbAlignedDimension");
        point(13, d.p1);
        point(14, d.p2);
        if (!aligned) {
            group(50, radToDeg(d.angle));
            sub("AcDbRotatedDimension");
        }
        break;
    }
    case Dimension::Kind::Radius:
    case Dimension::Kind::Diameter: {
        const bool diameter = d.kind == Dimension::Kind::Diameter;
        point(10, diameter ? d.p1 * 2.0 - d.p2 : d.p1);
        point(11, d.location);
        group(70, (diameter ? 3 : 4) | (ctx_.modern ? 32 : 0));
        if (!d.text.empty())
            group(1, encode(d.text));
        group(3, "STANDARD");
        sub(diameter ? "AcDbDiametricDimension" : "AcDbRadialDimension");
        point(15, d.p2);
        group(40, 0.0);
        break;
    }
    }
    if (ctx_.modern) {
        // Text and arrow size of this dimension, overriding the style as
        // AutoCAD does (DIMTXT 140, DIMASZ 41).
        group(1001, "ACAD");
        group(1000, "DSTYLE");
        group(1002, "{");
        group(1070, 140);
        group(1040, d.textHeight);
        group(1070, 41);
        group(1040, d.arrowSize);
        group(1002, "}");
    }
}

// DXF 2000: a HATCH entity, with the pattern lines scaled and rotated as the
// file wants them (the reader undoes this).
void DxfWriter::hatch(const std::string& layer, const Hatch& h)
{
    begin("HATCH", layer);
    sub("AcDbHatch");
    point(10, {0.0, 0.0});
    group(210, 0.0);
    group(220, 0.0);
    group(230, 1.0);
    group(2, h.solid() ? QStringLiteral("SOLID") : encode(h.pattern));
    group(70, h.solid() ? 1 : 0);
    group(71, 0);
    group(91, static_cast<int>(h.loops.size()));
    for (const Polyline& loop : h.loops) {
        group(92, 2);  // a polyline path
        const bool bulges = std::any_of(loop.bulges.begin(), loop.bulges.end(), [](double b) { return std::abs(b) > 1e-12; });
        group(72, bulges ? 1 : 0);
        group(73, 1);
        group(93, static_cast<int>(loop.points.size()));
        for (size_t i = 0; i < loop.points.size(); ++i) {
            group(10, loop.points[i].x);
            group(20, loop.points[i].y);
            if (bulges)
                group(42, loop.bulge(i));
        }
        group(97, 0);
    }
    group(75, 0);  // odd parity, as CAD4All fills
    group(76, 1);
    if (!h.solid()) {
        group(52, radToDeg(h.angle));
        group(41, h.scale);
        group(77, 0);
        group(78, static_cast<int>(h.lines.size()));
        const Transform rot = Transform::rotation({0, 0}, h.angle);
        for (const HatchPatternLine& l : h.lines) {
            const double angle = h.angle + l.angle;
            group(53, radToDeg(angle));
            const Vec2 base = h.origin + rot.applyVector(l.origin * h.scale);
            group(43, base.x);
            group(44, base.y);
            const Vec2 offset = Transform::rotation({0, 0}, angle).applyVector(l.offset * h.scale);
            group(45, offset.x);
            group(46, offset.y);
            group(79, static_cast<int>(l.dashes.size()));
            for (double dash : l.dashes)
                group(49, dash * h.scale);
        }
    }
    group(98, 0);
}

// R12 has no HATCH entity: AutoCAD R12 itself kept a hatch as an anonymous
// block of lines, inserted on the hatch's layer. A solid fill becomes SOLIDs.
void DxfWriter::hatchAsBlock(const std::string& layer, const Hatch& h)
{
    const QString name = QStringLiteral("*X%1").arg(++ctx_.hatchCount);
    DxfWriter block(ctx_, 0);
    block.blockBegin(name, {0.0, 0.0}, 1);
    bool dense = false;
    const auto lines = h.solid() ? std::vector<Line>{}
                                 : hatchLines(h, BBox::fromCorners({-1e300, -1e300}, {1e300, 1e300}), 500000, &dense);
    if (h.solid()) {
        // SOLID corners go in "bow tie" order: 1, 2, 4, 3.
        for (const auto& q : hatchTrapezoids(h))
            block.solid("0", std::array<Vec2, 4>{q[0], q[1], q[3], q[2]});
    } else if (dense) {
        // Far too many lines for a file: keep the outline only.
        for (const Polyline& loop : h.loops)
            block.shape("0", loop);
    } else {
        for (const Line& l : lines) {
            if (l.a == l.b) {
                block.shape("0", Point{l.a});
            } else {
                block.line("0", l.a, l.b);
            }
        }
    }
    block.blockEnd();
    ctx_.blocks += block.out_;

    begin("INSERT", layer);
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

QByteArray toDxf(const Document& doc, DxfVersion version)
{
    DxfContext ctx(version);
    const bool modern = ctx.modern;
    const unsigned modelSpace = modern ? ctx.blockRecord(QStringLiteral("*Model_Space")) : 0;
    const unsigned paperSpace = modern ? ctx.blockRecord(QStringLiteral("*Paper_Space")) : 0;

    // Fixed handles of the objects, below those given as the file is written.
    const unsigned layoutDict = 0x10, modelLayout = 0x11, paperLayout = 0x12;

    // Entities first: they define the anonymous blocks and use up handles.
    DxfWriter entities(ctx, modelSpace);
    // DXF has no walls, slabs, roofs or stairs: they go out as what they draw in plan.
    const std::map<EntityId, WallFaces> walls = joinedWalls(doc);
    // Only the current story: the others would lie on top of it.
    for (const auto& [id, e] : doc.entities()) {
        if (e.story != doc.currentStory())
            continue;
        entities.style_ = styleOf(e);
        const auto faces = walls.find(id);
        entities.object(e.layer, e.shape, faces == walls.end() ? nullptr : &faces->second);
    }
    // The blocks of the drawing, objects in block coordinates.
    QByteArray namedBlocks;
    for (const auto& [key, b] : doc.blocks()) {
        const QString name = DxfWriter::encode(b->name);
        DxfWriter block(ctx, modern ? ctx.blockRecord(name) : 0);
        block.blockBegin(name, b->base, 0);
        for (const BlockItem& item : b->items) {
            block.style_ = {item.color, item.linetype, item.lineweight};
            block.colorByBlock_ = item.colorByBlock;
            block.object(item.layer, item.shape, nullptr);
        }
        block.blockEnd();
        namedBlocks += block.out_;
    }
    QByteArray layoutBlocks;
    if (modern) {
        DxfWriter ms(ctx, modelSpace);
        ms.blockBegin(QStringLiteral("*Model_Space"), {0, 0}, 0);
        ms.blockEnd();
        DxfWriter ps(ctx, paperSpace);
        ps.blockBegin(QStringLiteral("*Paper_Space"), {0, 0}, 0);
        ps.blockEnd();
        layoutBlocks = ms.out_ + ps.out_;
    }

    // Tables and objects, with their handles.
    DxfWriter t(ctx, 0);
    auto table = [&](const char* name, int count) {
        t.group(0, "TABLE");
        t.group(2, name);
        unsigned h = 0;
        if (modern) {
            h = ctx.handle();
            t.hex(5, h);
            t.hex(330, 0);
            t.group(100, "AcDbSymbolTable");
        }
        t.group(70, count);
        return h;
    };
    auto entry = [&](const char* type, unsigned owner, const char* subclass) {
        t.group(0, type);
        if (modern) {
            t.hex(5, ctx.handle());
            t.hex(330, owner);
            t.group(100, "AcDbSymbolTableRecord");
            t.group(100, subclass);
        }
    };

    if (modern) {
        const unsigned vports = table("VPORT", 1);
        entry("VPORT", vports, "AcDbViewportTableRecord");
        t.group(2, "*ACTIVE");
        t.group(70, 0);
        const BBox ext = doc.extents();
        const Vec2 c = ext.valid() ? (ext.min + ext.max) * 0.5 : Vec2{0, 0};
        t.group(12, c.x);
        t.group(22, c.y);
        t.group(40, ext.valid() ? std::max(ext.height(), ext.width() / 1.5) * 1.1 : 297.0);
        t.group(41, 1.5);
        t.group(0, "ENDTAB");
    }

    std::vector<const Linetype*> linetypes;
    for (const Linetype& lt : doc.linetypes())
        if (!modern || (linetypeKey(lt.name) != "BYBLOCK" && linetypeKey(lt.name) != kByLayer))
            linetypes.push_back(&lt);
    const unsigned ltypes = table("LTYPE", static_cast<int>(linetypes.size()) + (modern ? 2 : 0));
    if (modern) {
        // AutoCAD expects the two pseudo linetypes in the table.
        for (const char* name : {"ByBlock", "ByLayer"}) {
            entry("LTYPE", ltypes, "AcDbLinetypeTableRecord");
            t.group(2, name);
            t.group(70, 0);
            t.group(3, "");
            t.group(72, 65);
            t.group(73, 0);
            t.group(40, 0.0);
        }
    }
    for (const Linetype* lt : linetypes) {
        entry("LTYPE", ltypes, "AcDbLinetypeTableRecord");
        t.group(2, DxfWriter::encode(lt->name));
        t.group(70, 0);
        t.group(3, DxfWriter::encode(lt->description));
        t.group(72, 65);
        t.group(73, static_cast<int>(lt->pattern.size()));
        t.group(40, lt->patternLength());
        for (double d : lt->pattern) {
            t.group(49, d);
            if (modern)
                t.group(74, 0);
        }
    }
    t.group(0, "ENDTAB");

    const unsigned layers = table("LAYER", static_cast<int>(doc.layers().size()));
    for (const Layer& l : doc.layers()) {
        entry("LAYER", layers, "AcDbLayerTableRecord");
        t.group(2, DxfWriter::layerName(l.name));
        t.group(70, l.locked ? 4 : 0);
        const auto exact = exactAci(l.color);
        const int aci = exact ? *exact : colorToAci(l.color);
        t.group(62, l.visible ? aci : -aci);
        t.group(6, DxfWriter::encode(l.linetype));
        if (modern) {
            if (!exact)
                t.group(420, static_cast<int>((l.color.r << 16) | (l.color.g << 8) | l.color.b));
            t.group(370, l.lineweight);
            t.hex(390, 0xF);  // the plot style placeholder in OBJECTS
        }
    }
    t.group(0, "ENDTAB");

    const unsigned styles = table("STYLE", 1);
    entry("STYLE", styles, "AcDbTextStyleTableRecord");
    t.group(2, "STANDARD");
    t.group(70, 0);
    t.group(40, 0.0);
    t.group(41, 1.0);
    t.group(50, 0.0);
    t.group(71, 0);
    t.group(42, 2.5);
    t.group(3, "txt");
    t.group(4, "");
    t.group(0, "ENDTAB");

    if (modern) {
        for (const char* name : {"VIEW", "UCS"}) {
            table(name, 0);
            t.group(0, "ENDTAB");
        }
    }

    const unsigned appids = table("APPID", modern ? 2 : 1);
    for (const char* name : modern ? std::vector<const char*>{"ACAD", DxfWriter::kAppName} : std::vector<const char*>{"ACAD"}) {
        entry("APPID", appids, "AcDbRegAppTableRecord");
        t.group(2, name);
        t.group(70, 0);
    }
    t.group(0, "ENDTAB");

    const unsigned dimstyles = table("DIMSTYLE", 1);
    if (modern)
        t.group(100, "AcDbDimStyleTable");
    t.group(0, "DIMSTYLE");
    if (modern) {
        // Dimension styles give their handle with 105, not 5.
        t.hex(105, ctx.handle());
        t.hex(330, dimstyles);
        t.group(100, "AcDbSymbolTableRecord");
        t.group(100, "AcDbDimStyleTableRecord");
    }
    t.group(2, "STANDARD");
    t.group(70, 0);
    t.group(0, "ENDTAB");

    if (modern) {
        const unsigned records = table("BLOCK_RECORD", static_cast<int>(ctx.blockRecords.size()));
        // The records were numbered as the blocks were written; fix their owner now.
        for (const auto& [name, h] : ctx.blockRecords) {
            t.group(0, "BLOCK_RECORD");
            t.hex(5, h);
            t.hex(330, records);
            t.group(100, "AcDbSymbolTableRecord");
            t.group(100, "AcDbBlockTableRecord");
            t.group(2, name);
            if (h == modelSpace)
                t.hex(340, modelLayout);
            else if (h == paperSpace)
                t.hex(340, paperLayout);
        }
        t.group(0, "ENDTAB");
    }

    // OBJECTS: the root dictionary with the group dictionary AutoCAD wants,
    // the layouts of the model and of one sheet, and the plot style
    // placeholder the layers point to.
    QByteArray objects;
    if (modern) {
        DxfWriter o(ctx, 0);
        const unsigned root = 0xC, groups = 0xD;
        o.group(0, "DICTIONARY");
        o.hex(5, root);
        o.hex(330, 0);
        o.group(100, "AcDbDictionary");
        o.group(281, 1);
        o.group(3, "ACAD_GROUP");
        o.hex(350, groups);
        o.group(3, "ACAD_LAYOUT");
        o.hex(350, layoutDict);
        o.group(3, "ACAD_PLOTSTYLENAME");
        o.hex(350, 0xE);
        o.group(0, "DICTIONARY");
        o.hex(5, groups);
        o.hex(330, root);
        o.group(100, "AcDbDictionary");
        o.group(281, 1);
        o.group(0, "DICTIONARY");
        o.hex(5, layoutDict);
        o.hex(330, root);
        o.group(100, "AcDbDictionary");
        o.group(281, 1);
        o.group(3, "Model");
        o.hex(350, modelLayout);
        o.group(3, "Layout1");
        o.hex(350, paperLayout);
        // The model and one empty sheet, as AutoCAD writes them.
        for (const bool model : {true, false}) {
            o.group(0, "LAYOUT");
            o.hex(5, model ? modelLayout : paperLayout);
            o.hex(330, layoutDict);
            o.group(100, "AcDbPlotSettings");
            o.group(1, "");
            o.group(4, "A3");
            o.group(6, "");
            for (const auto& [code, value] : std::vector<std::pair<int, double>>{
                     {40, 7.5}, {41, 20.0}, {42, 7.5}, {43, 20.0}, {44, 420.0}, {45, 297.0}, {46, 0.0}, {47, 0.0},
                     {48, 0.0}, {49, 0.0}, {140, 0.0}, {141, 0.0}, {142, 1.0}, {143, 1.0}})
                o.group(code, value);
            o.group(70, model ? 1024 : 0);
            o.group(72, 1);
            o.group(73, 0);
            o.group(74, 5);
            o.group(7, "");
            o.group(75, 16);
            o.group(76, 0);
            o.group(77, 2);
            o.group(78, 300);
            o.group(147, 1.0);
            o.group(148, 0.0);
            o.group(149, 0.0);
            o.group(100, "AcDbLayout");
            o.group(1, model ? "Model" : "Layout1");
            o.group(70, 1);
            o.group(71, model ? 0 : 1);
            // Limits: 2D points.
            o.group(10, 0.0);
            o.group(20, 0.0);
            o.group(11, 420.0);
            o.group(21, 297.0);
            o.point(12, {0.0, 0.0});
            o.point(14, {1e20, 1e20});
            o.point(15, {-1e20, -1e20});
            o.group(146, 0.0);
            o.point(13, {0.0, 0.0});
            o.point(16, {1.0, 0.0});
            o.point(17, {0.0, 1.0});
            o.group(76, 1);
            o.hex(330, model ? modelSpace : paperSpace);
        }
        o.group(0, "ACDBDICTIONARYWDFLT");
        o.hex(5, 0xE);
        o.hex(330, root);
        o.group(100, "AcDbDictionary");
        o.group(281, 1);
        o.group(3, "Normal");
        o.hex(350, 0xF);
        o.group(100, "AcDbDictionaryWithDefault");
        o.hex(340, 0xF);
        o.group(0, "ACDBPLACEHOLDER");
        o.hex(5, 0xF);
        o.hex(330, 0xE);
        objects = o.out_;
    }

    // Header last, now that the handles are all given.
    DxfWriter w(ctx, 0);
    const BBox ext = doc.extents();
    w.group(0, "SECTION");
    w.group(2, "HEADER");
    w.group(9, "$ACADVER");
    w.group(1, modern ? "AC1015" : "AC1009");
    w.group(9, "$DWGCODEPAGE");
    w.group(3, "ANSI_1252");
    if (modern) {
        w.group(9, "$HANDSEED");
        w.hex(5, ctx.next);
        w.group(9, "$MEASUREMENT");
        w.group(70, 1);
    }
    w.group(9, "$EXTMIN");
    w.point(10, ext.valid() ? ext.min : Vec2{0, 0});
    w.group(9, "$EXTMAX");
    w.point(10, ext.valid() ? ext.max : Vec2{0, 0});
    w.group(9, "$LTSCALE");
    w.group(40, doc.linetypeScale());
    w.group(0, "ENDSEC");
    if (modern) {
        w.group(0, "SECTION");
        w.group(2, "CLASSES");
        // The objects of OBJECTS that are classes rather than built-in types.
        for (const auto& [name, cpp] : std::vector<std::pair<const char*, const char*>>{
                 {"ACDBDICTIONARYWDFLT", "AcDbDictionaryWithDefault"},
                 {"ACDBPLACEHOLDER", "AcDbPlaceHolder"},
                 {"LAYOUT", "AcDbLayout"}}) {
            w.group(0, "CLASS");
            w.group(1, name);
            w.group(2, cpp);
            w.group(3, "ObjectDBX Classes");
            w.group(90, 0);
            w.group(280, 0);
            w.group(281, 0);
        }
        w.group(0, "ENDSEC");
    }
    w.group(0, "SECTION");
    w.group(2, "TABLES");
    w.out_ += t.out_;
    w.group(0, "ENDSEC");
    w.group(0, "SECTION");
    w.group(2, "BLOCKS");
    w.out_ += layoutBlocks;
    w.out_ += ctx.blocks;
    w.out_ += namedBlocks;
    w.group(0, "ENDSEC");
    w.group(0, "SECTION");
    w.group(2, "ENTITIES");
    w.out_ += entities.out_;
    w.group(0, "ENDSEC");
    if (modern) {
        w.group(0, "SECTION");
        w.group(2, "OBJECTS");
        w.out_ += objects;
        w.group(0, "ENDSEC");
    }
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

bool saveDxf(const Document& doc, const QString& path, QString* error, DxfVersion version)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(toDxf(doc, version));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace cad::io
