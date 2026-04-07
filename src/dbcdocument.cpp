#include "dbcdocument.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>
#include <QtGlobal>
#include <algorithm>

namespace {
QString toNumberString(double value)
{
    return QString::number(value, 'g', 12);
}

QString buildMultiplexToken(const DbcSignal& dbcSig)
{
    if (dbcSig.mode.compare("Multiplexor", Qt::CaseInsensitive) == 0) {
        return " M";
    }

    if (dbcSig.mode == "M=") {
        bool ok = false;
        const QString hexText = dbcSig.modeValueHex.trimmed();
        const quint64 value = hexText.isEmpty() ? 0ull : hexText.toULongLong(&ok, 16);
        const quint64 muxValue = ok ? value : 0ull;
        return QString(" m%1").arg(muxValue);
    }

    return QString{};
}

quint32 rawIdFromMessage(const DbcMessage& message)
{
    quint32 rawId = message.id & 0x1FFFFFFFu;
    if (message.isExtended) {
        rawId |= 0x80000000u;
    }
    return rawId;
}

QList<DbcValueEntry> parseValueEntries(const QString& text)
{
    QList<DbcValueEntry> entries;
    QRegularExpression pairRe("(-?\\d+)\\s+\"([^\"]*)\"");;
    auto it = pairRe.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        bool ok = false;
        const qint64 val = m.captured(1).toLongLong(&ok);
        if (ok) {
            entries.append(DbcValueEntry{val, m.captured(2)});
        }
    }
    return entries;
}

QString serializeValueEntries(const QList<DbcValueEntry>& entries)
{
    QStringList parts;
    for (const DbcValueEntry& e : entries) {
        parts << QString("%1 \"%2\"").arg(e.rawValue).arg(e.label);
    }
    return parts.join(' ');
}
} // end anonymous namespace

bool DbcDocument::loadFromFile(const QString& filePath, DbcDatabase& outDatabase, QString& error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QString("Failed to open file: %1").arg(file.errorString());
        return false;
    }

    DbcDatabase database;
    QTextStream in(&file);

    QRegularExpression messageRegex(R"(^BO_\s+(\d+)\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(\d+)\s+([A-Za-z_][A-Za-z0-9_]*)\s*$)");
    QRegularExpression signalRegex(
        R"(^SG_\s+([A-Za-z_][A-Za-z0-9_]*)\s*(M|m(?:0x[0-9A-Fa-f]+|\d+))?\s*:\s*(\d+)\|(\d+)@([01])([+-])\s+\(([-+0-9\.eE]+),([-+0-9\.eE]+)\)\s+\[([-+0-9\.eE]+)\|([-+0-9\.eE]+)\]\s+\"([^\"]*)\"\s*(.*)$)");
    QRegularExpression cycleRegex(R"(^BA_\s+"GenMsgCycleTime"\s+BO_\s+(\d+)\s+(\d+)\s*;\s*$)");
    QRegularExpression messageCommentRegex(R"dbc(^CM_\s+BO_\s+(\d+)\s+"((?:\\"|[^"])*)"\s*;\s*$)dbc");
    QRegularExpression signalCommentRegex(R"dbc(^CM_\s+SG_\s+(\d+)\s+([A-Za-z_][A-Za-z0-9_]*)\s+"((?:\\"|[^"])*)"\s*;\s*$)dbc");    QRegularExpression signalValueRegex(R"(^VAL_\s+(\d+)\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.*?)\s*;\s*$)");
    // BA_DEF_  [BU_|BO_|SG_]  "Name"  TYPE  [params] ;   (escaped strings — raw literals break on )" inside pattern)
    QRegularExpression attrDefRegex("^BA_DEF_\\s*(BU_|BO_|SG_)?\\s*\"([^\"]+)\"\\s+(INT|FLOAT|STRING|ENUM|HEX)\\s*(.*?)\\s*;?\\s*$");
    QRegularExpression attrDefDefRegex("^BA_DEF_DEF_\\s+\"([^\"]+)\"\\s+(.*?)\\s*;?\\s*$");
    QRegularExpression enumValRe("\"([^\"]*)\"");
    // Built-in attribute names managed internally — not surfaced to the user
    static const QStringList kReservedAttrNames{"BusType", "DBName", "GenMsgCycleTime"};
    QHash<QString, QString> attrDefaults;   // name → raw default string
    QHash<quint32, int> msgByRawId;            // rawId → index in database.messages

    int currentMessageIndex = -1;
    int lineNo = 0;

    while (!in.atEnd()) {
        const QString rawLine = in.readLine();
        lineNo += 1;
        const QString line = rawLine.trimmed();

        if (line.isEmpty()) {
            continue;
        }

        if (line.startsWith("VAL_TABLE_")) {
            // Ignore the bare "VAL_TABLE_" NS_ keyword; only store actual table definitions
            if (line.length() > 10) {
                const QString payload = line.mid(10).trimmed();
                const int spacePos = payload.indexOf(' ');
                if (spacePos > 0) {
                    DbcValueTable vt;
                    vt.name = payload.left(spacePos).trimmed();
                    QString entriesText = payload.mid(spacePos + 1).trimmed();
                    if (entriesText.endsWith(';')) {
                        entriesText.chop(1);
                        entriesText = entriesText.trimmed();
                    }
                    vt.entries = parseValueEntries(entriesText);
                    database.valueTables.append(vt);
                }
            }
            continue;
        }

        if (line.startsWith("VAL_")) {
            const auto valueMatch = signalValueRegex.match(line);
            if (valueMatch.hasMatch()) {
                const quint32 rawId = static_cast<quint32>(valueMatch.captured(1).toULongLong());
                const QString sigName = valueMatch.captured(2);
                const QString valueDefs = valueMatch.captured(3).trimmed();
                const int idx = msgByRawId.value(rawId, -1);
                if (idx >= 0) {
                    for (DbcSignal& dbcSig : database.messages[idx].signalList) {
                        if (dbcSig.name == sigName) {
                            dbcSig.valueEntries = parseValueEntries(valueDefs);
                            break;
                        }
                    }
                }
            }
            continue;
        }

        if (line.startsWith("CM_")) {
            const auto messageCommentMatch = messageCommentRegex.match(line);
            if (messageCommentMatch.hasMatch()) {
                const quint32 rawId = static_cast<quint32>(messageCommentMatch.captured(1).toULongLong());
                QString commentText = messageCommentMatch.captured(2);
                commentText.replace("\\n", "\n");      // unescape newlines
                commentText.replace("\\\\", "\\");    // unescape backslashes
                commentText.replace("\\\"", "\"");     // unescape quotes
                const int idx = msgByRawId.value(rawId, -1);
                if (idx >= 0) { database.messages[idx].comment = commentText; }
                continue;
            }

            const auto signalCommentMatch = signalCommentRegex.match(line);
            if (signalCommentMatch.hasMatch()) {
                const quint32 rawId = static_cast<quint32>(signalCommentMatch.captured(1).toULongLong());
                const QString signalName = signalCommentMatch.captured(2);
                QString commentText = signalCommentMatch.captured(3);
                commentText.replace("\\n", "\n");      // unescape newlines
                commentText.replace("\\\\", "\\");    // unescape backslashes
                commentText.replace("\\\"", "\"");     // unescape quotes
                const int idx = msgByRawId.value(rawId, -1);
                if (idx >= 0) {
                    for (DbcSignal& dbcSig : database.messages[idx].signalList) {
                        if (dbcSig.name == signalName) {
                            dbcSig.comment = commentText;
                            break;
                        }
                    }
                }
                continue;
            }
        }

        // BA_DEF_DEF_
        if (line.startsWith("BA_DEF_DEF_")) {
            const auto m = attrDefDefRegex.match(line);
            if (m.hasMatch()) { attrDefaults[m.captured(1)] = m.captured(2).trimmed(); }
            continue;
        }

        // BA_DEF_
        if (line.startsWith("BA_DEF_")) {
            const auto m = attrDefRegex.match(line);
            if (m.hasMatch()) {
                const QString attrName = m.captured(2);
                if (!kReservedAttrNames.contains(attrName)) {
                    DbcAttributeDef attr;
                    attr.name = attrName;
                    const QString objTok = m.captured(1).trimmed();
                    if      (objTok == "BU_") { attr.objectType = DbcAttributeDef::ObjectType::Node; }
                    else if (objTok == "BO_") { attr.objectType = DbcAttributeDef::ObjectType::Message; }
                    else if (objTok == "SG_") { attr.objectType = DbcAttributeDef::ObjectType::Signal; }
                    else                      { attr.objectType = DbcAttributeDef::ObjectType::Network; }
                    const QString typeTok = m.captured(3);
                    const QString params  = m.captured(4).trimmed();
                    if (typeTok == "INT") {
                        attr.valueType = DbcAttributeDef::ValueType::Integer;
                        const QStringList p = params.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                        if (p.size() >= 2) { attr.minimum = p[0]; attr.maximum = p[1]; }
                    } else if (typeTok == "FLOAT") {
                        attr.valueType = DbcAttributeDef::ValueType::Float;
                        const QStringList p = params.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                        if (p.size() >= 2) { attr.minimum = p[0]; attr.maximum = p[1]; }
                    } else if (typeTok == "HEX") {
                        attr.valueType = DbcAttributeDef::ValueType::Hex;
                        const QStringList p = params.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                        if (p.size() >= 2) { attr.minimum = p[0]; attr.maximum = p[1]; }
                    } else if (typeTok == "STRING") {
                        attr.valueType = DbcAttributeDef::ValueType::String;
                    } else if (typeTok == "ENUM") {
                        attr.valueType = DbcAttributeDef::ValueType::Enumeration;
                        auto it = enumValRe.globalMatch(params);
                        while (it.hasNext()) { attr.enumValues << it.next().captured(1); }
                    }
                    database.attributes.append(attr);
                }
            }
            continue;
        }

        if (line.startsWith("BA_")) {
            const auto cycleMatch = cycleRegex.match(line);
            if (cycleMatch.hasMatch()) {
                const quint32 rawId = static_cast<quint32>(cycleMatch.captured(1).toULongLong());
                const int cycleMs = cycleMatch.captured(2).toInt();
                const int idx = msgByRawId.value(rawId, -1);
                if (idx >= 0) { database.messages[idx].cycleTimeMs = cycleMs; }
                continue;
            }
        }

        if (line.startsWith("BU_")) {
            const QString payload = line.mid(3).trimmed();
            if (payload.startsWith(':')) {
                const QString nodeText = payload.mid(1).trimmed();
                if (!nodeText.isEmpty()) {
                    const QStringList names = nodeText.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                    for (const QString& nodeName : names) {
                        database.nodes.append(DbcNode{nodeName, QString{}, QString{}});
                    }
                }
            }
            continue;
        }

        if (line.startsWith("BO_")) {
            const auto messageMatch = messageRegex.match(line);
            if (messageMatch.hasMatch()) {
                DbcMessage message;
                const quint32 rawId = static_cast<quint32>(messageMatch.captured(1).toULongLong());
                message.isExtended = (rawId & 0x80000000u) != 0u || rawId > 0x7FFu;
                message.id = message.isExtended ? (rawId & 0x1FFFFFFFu) : rawId;
                message.name = messageMatch.captured(2);
                message.dlc = messageMatch.captured(3).toInt();
                message.transmitter = messageMatch.captured(4);

                database.messages.append(message);
                currentMessageIndex = database.messages.size() - 1;
                msgByRawId.insert(rawIdFromMessage(database.messages.last()), currentMessageIndex);
                continue;
            }
        }

        if (line.startsWith("SG_")) {
            const auto signalMatch = signalRegex.match(line);
            if (signalMatch.hasMatch()) {
            if (currentMessageIndex < 0 || currentMessageIndex >= database.messages.size()) {
                error = QString("Signal found before any message at line %1").arg(lineNo);
                return false;
            }

            DbcSignal dbcSig;
            dbcSig.name = signalMatch.captured(1);
            const QString muxToken = signalMatch.captured(2).trimmed();
            if (muxToken.compare("M", Qt::CaseInsensitive) == 0) {
                dbcSig.mode = "Multiplexor";
                dbcSig.modeValueHex.clear();
            } else if (muxToken.startsWith('m', Qt::CaseInsensitive)) {
                dbcSig.mode = "M=";
                const QString muxValueText = muxToken.mid(1);
                bool ok = false;
                quint64 muxValue = 0ull;
                if (muxValueText.startsWith("0x", Qt::CaseInsensitive)) {
                    muxValue = muxValueText.mid(2).toULongLong(&ok, 16);
                } else {
                    muxValue = muxValueText.toULongLong(&ok, 10);
                }
                dbcSig.modeValueHex = ok ? QString::number(muxValue, 16).toUpper() : QString{};
            } else {
                dbcSig.mode = "Signal";
                dbcSig.modeValueHex.clear();
            }

            dbcSig.startBit = signalMatch.captured(3).toInt();
            dbcSig.bitLength = signalMatch.captured(4).toInt();
            dbcSig.byteOrder = signalMatch.captured(5) == "1" ? "Intel" : "Motorola";
            dbcSig.valueType = signalMatch.captured(6) == "-" ? "Signed" : "Unsigned";
            dbcSig.factor = signalMatch.captured(7).toDouble();
            dbcSig.offset = signalMatch.captured(8).toDouble();
            dbcSig.minimum = signalMatch.captured(9).toDouble();
            dbcSig.maximum = signalMatch.captured(10).toDouble();
            dbcSig.unit = signalMatch.captured(11);

            const QString receiverText = signalMatch.captured(12).trimmed();
            if (!receiverText.isEmpty()) {
                dbcSig.receivers = receiverText.split(',', Qt::SkipEmptyParts);
                for (QString& receiver : dbcSig.receivers) {
                    receiver = receiver.trimmed();
                }
            }

            database.messages[currentMessageIndex].signalList.append(dbcSig);
                continue;
            }
        }
    }

    // Auto-link signal value entries to named global VAL_TABLE_s by exact entry match.
    // Build a hash of valuetable content fingerprint → name for O(1) lookup.
    QHash<QString, QString> vtByContent;
    for (const DbcValueTable& vt : database.valueTables) {
        QString key;
        key.reserve(vt.entries.size() * 12);
        for (const DbcValueEntry& e : vt.entries) {
            key += QString::number(e.rawValue) + '\x01' + e.label + '\x02';
        }
        vtByContent.insert(key, vt.name);
    }
    for (DbcMessage& msg : database.messages) {
        for (DbcSignal& sig : msg.signalList) {
            if (sig.valueEntries.isEmpty()) { continue; }
            QString key;
            key.reserve(sig.valueEntries.size() * 12);
            for (const DbcValueEntry& e : sig.valueEntries) {
                key += QString::number(e.rawValue) + '\x01' + e.label + '\x02';
            }
            const auto it = vtByContent.constFind(key);
            if (it != vtByContent.constEnd()) { sig.valueTableName = it.value(); }
        }
    }

    // Apply parsed BA_DEF_DEF_ defaults to user-defined attributes
    for (DbcAttributeDef& attr : database.attributes) {
        if (!attrDefaults.contains(attr.name)) { continue; }
        QString defVal = attrDefaults[attr.name];
        if (defVal.startsWith('"') && defVal.endsWith('"') && defVal.size() >= 2) {
            defVal = defVal.mid(1, defVal.size() - 2);
        }
        attr.defaultValue = defVal;
    }

    outDatabase = database;
    return true;
}

bool DbcDocument::saveToFile(const QString& filePath, const DbcDatabase& database, QString& error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        error = QString("Failed to write file: %1").arg(file.errorString());
        return false;
    }

    QTextStream out(&file);

    out << "VERSION \"\"\n\n";

    out << "NS_ :\n"
        << "\tNS_DESC_\n"
        << "\tCM_\n"
        << "\tBA_DEF_\n"
        << "\tBA_\n"
        << "\tVAL_\n"
        << "\tCAT_DEF_\n"
        << "\tCAT_\n"
        << "\tFILTER\n"
        << "\tBA_DEF_DEF_\n"
        << "\tEV_DATA_\n"
        << "\tENVVAR_DATA_\n"
        << "\tSGTYPE_\n"
        << "\tSGTYPE_VAL_\n"
        << "\tBA_DEF_SGTYPE_\n"
        << "\tBA_SGTYPE_\n"
        << "\tSIG_TYPE_REF_\n"
        << "\tVAL_TABLE_\n"
        << "\tSIG_GROUP_\n"
        << "\tSIG_VALTYPE_\n"
        << "\tSIGTYPE_VALTYPE_\n"
        << "\tBO_TX_BU_\n"
        << "\tBA_DEF_REL_\n"
        << "\tBA_REL_\n"
        << "\tBA_DEF_DEF_REL_\n"
        << "\tBU_SG_REL_\n"
        << "\tBU_EV_REL_\n"
        << "\tBU_BO_REL_\n"
        << "\tSG_MUL_VAL_\n"
        << "\n";

    out << "BS_:\n\n";

    out << "BU_:";
    for (const DbcNode& node : database.nodes) {
        out << " " << node.name;
    }
    out << "\n\n";

    // VAL_TABLE_ global named value tables (plus any named by signals but not yet global)
    QList<DbcValueTable> tablesToWrite = database.valueTables;
    for (const DbcMessage& message : database.messages) {
        for (const DbcSignal& sig : message.signalList) {
            if (sig.valueTableName.isEmpty() || sig.valueEntries.isEmpty()) { continue; }
            bool found = false;
            for (const DbcValueTable& vt : tablesToWrite) {
                if (vt.name == sig.valueTableName) { found = true; break; }
            }
            if (!found) {
                tablesToWrite.append(DbcValueTable{sig.valueTableName, sig.valueEntries});
            }
        }
    }
    for (const DbcValueTable& vt : tablesToWrite) {
        out << "VAL_TABLE_ " << vt.name << " " << serializeValueEntries(vt.entries) << " ;\n";
    }
    if (!tablesToWrite.isEmpty()) {
        out << "\n";
    }

    // Messages
    for (const DbcMessage& message : database.messages) {
        out << "BO_ " << rawIdFromMessage(message) << " " << message.name << ": " << message.dlc << " " << message.transmitter << "\n";

        for (const DbcSignal& dbcSig : message.signalList) {
            const QString byteOrder = dbcSig.byteOrder.compare("Motorola", Qt::CaseInsensitive) == 0 ? "0" : "1";
            const QString valueType = dbcSig.valueType.compare("Signed", Qt::CaseInsensitive) == 0 ? "-" : "+";
            const QString receivers = dbcSig.receivers.isEmpty() ? "Vector__XXX" : dbcSig.receivers.join(',');
            const QString multiplexToken = buildMultiplexToken(dbcSig);

            out << " SG_ " << dbcSig.name << multiplexToken
                << " : " << dbcSig.startBit << "|" << dbcSig.bitLength << "@" << byteOrder << valueType
                << " (" << toNumberString(dbcSig.factor) << "," << toNumberString(dbcSig.offset) << ")"
                << " [" << toNumberString(dbcSig.minimum) << "|" << toNumberString(dbcSig.maximum) << "]"
                << " \"" << dbcSig.unit << "\" " << receivers << "\n";
        }

        out << "\n";
    }

    // CM_ comments
    for (const DbcMessage& message : database.messages) {
        if (!message.comment.isEmpty()) {
            QString commentText = message.comment;
            commentText.replace("\\", "\\\\");    // escape backslashes first
            commentText.replace("\r", "");         // strip carriage returns
            commentText.replace("\n", "\\n");      // escape newlines
            commentText.replace("\"", "\\\"");     // escape quotes
            out << "CM_ BO_ " << rawIdFromMessage(message) << " \"" << commentText << "\";\n";
        }
        for (const DbcSignal& dbcSig : message.signalList) {
            if (dbcSig.comment.isEmpty()) {
                continue;
            }
            QString commentText = dbcSig.comment;
            commentText.replace("\\", "\\\\");    // escape backslashes first
            commentText.replace("\r", "");         // strip carriage returns
            commentText.replace("\n", "\\n");      // escape newlines
            commentText.replace("\"", "\\\"");     // escape quotes
            out << "CM_ SG_ " << rawIdFromMessage(message) << " " << dbcSig.name << " \"" << commentText << "\";\n";
        }
    }

    // BA_DEF_ / BA_DEF_DEF_ / BA_ sections
    const bool hasGenMsgCycleTime = std::any_of(
        database.messages.begin(), database.messages.end(),
        [](const DbcMessage& m) { return m.cycleTimeMs != 0; });

    out << "BA_DEF_  \"BusType\" STRING ;\n";
    out << "BA_DEF_  \"DBName\" STRING ;\n";
    if (hasGenMsgCycleTime) {
        out << "BA_DEF_ BO_  \"GenMsgCycleTime\" INT 0 65535;\n";
    }

    // User-defined attribute definitions
    for (const DbcAttributeDef& attr : database.attributes) {
        const QString objTok = [&]() -> QString {
            switch (attr.objectType) {
                case DbcAttributeDef::ObjectType::Node:    return "BU_ ";
                case DbcAttributeDef::ObjectType::Message: return "BO_ ";
                case DbcAttributeDef::ObjectType::Signal:  return "SG_ ";
                default: return QString{};
            }
        }();
        QString typePart;
        switch (attr.valueType) {
            case DbcAttributeDef::ValueType::Integer:
                typePart = QString("INT %1 %2")
                    .arg(attr.minimum.isEmpty() ? "0" : attr.minimum,
                         attr.maximum.isEmpty() ? "0" : attr.maximum);
                break;
            case DbcAttributeDef::ValueType::Float:
                typePart = QString("FLOAT %1 %2")
                    .arg(attr.minimum.isEmpty() ? "0" : attr.minimum,
                         attr.maximum.isEmpty() ? "0" : attr.maximum);
                break;
            case DbcAttributeDef::ValueType::String:
                typePart = "STRING";
                break;
            case DbcAttributeDef::ValueType::Enumeration: {
                QStringList quoted;
                for (const QString& v : attr.enumValues) { quoted << QString("\"%1\"").arg(v); }
                typePart = "ENUM " + quoted.join(',');
                break;
            }
            case DbcAttributeDef::ValueType::Hex:
                typePart = QString("HEX %1 %2")
                    .arg(attr.minimum.isEmpty() ? "0" : attr.minimum,
                         attr.maximum.isEmpty() ? "0" : attr.maximum);
                break;
        }
        out << "BA_DEF_ " << objTok << " \"" << attr.name << "\" " << typePart << " ;\n";
    }

    out << "BA_DEF_DEF_  \"BusType\" \"\";\n";
    out << "BA_DEF_DEF_  \"DBName\" \"\";\n";
    if (hasGenMsgCycleTime) {
        out << "BA_DEF_DEF_  \"GenMsgCycleTime\" 0;\n";
    }

    // User-defined attribute defaults
    for (const DbcAttributeDef& attr : database.attributes) {
        const bool needsQuotes = (attr.valueType == DbcAttributeDef::ValueType::String ||
                                  attr.valueType == DbcAttributeDef::ValueType::Enumeration);
        const QString defVal = needsQuotes
            ? QString("\"%1\"").arg(attr.defaultValue)
            : (attr.defaultValue.isEmpty() ? "0" : attr.defaultValue);
        out << "BA_DEF_DEF_  \"" << attr.name << "\" " << defVal << ";\n";
    }

    out << "BA_ \"BusType\" \"CAN\";\n";
    out << "BA_ \"DBName\" \"\";\n";
    if (hasGenMsgCycleTime) {
        for (const DbcMessage& message : database.messages) {
            if (message.cycleTimeMs != 0) {
                out << "BA_ \"GenMsgCycleTime\" BO_ " << rawIdFromMessage(message)
                    << " " << message.cycleTimeMs << ";\n";
            }
        }
    }
    out << "\n";

    // VAL_ signal value definitions
    for (const DbcMessage& message : database.messages) {
        for (const DbcSignal& dbcSig : message.signalList) {
            if (!dbcSig.valueEntries.isEmpty()) {
                out << "VAL_ " << rawIdFromMessage(message) << " " << dbcSig.name
                    << " " << serializeValueEntries(dbcSig.valueEntries) << " ;\n";
            }
        }
    }

    out.flush();
    if (out.status() != QTextStream::Ok || file.error() != QFile::NoError) {
        error = QString("Write error: %1").arg(file.errorString());
        return false;
    }
    return true;
}
