#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QList>
#include <QVector>

struct DbcValueEntry {
    qint64 rawValue = 0;
    QString label;
    bool operator==(const DbcValueEntry& o) const { return rawValue == o.rawValue && label == o.label; }
};

struct DbcValueTable {
    QString name;
    QList<DbcValueEntry> entries;
};

struct DbcNode {
    QString name;
    QString address;
    QString comment;
};

struct DbcSignal {
    QString name;
    QString mode = "Signal";
    QString modeValueHex;
    int startBit = 0;
    int bitLength = 1;
    QString byteOrder = "Intel";
    QString valueType = "Unsigned";
    double factor = 1.0;
    double offset = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    QString unit;
    QString valueTableName;
    QList<DbcValueEntry> valueEntries;
    QString comment;
    QStringList receivers;
};

struct DbcMessage {
    quint32 id = 0;
    bool isExtended = false;
    QString name;
    int dlc = 8;
    int cycleTimeMs = 0;
    QString transmitter;
    QString comment;
    QVector<DbcSignal> signalList;
};

struct DbcAttributeDef {
    enum class ObjectType { Network, Node, Message, Signal };
    enum class ValueType  { Integer, Float, String, Enumeration, Hex };

    QString     name;
    ObjectType  objectType  = ObjectType::Network;
    ValueType   valueType   = ValueType::Integer;

    QString     defaultValue;   // string/enum: the chosen default; int/float/hex: decimal string
    QString     minimum;        // Int / Float / Hex
    QString     maximum;        // Int / Float / Hex
    QStringList enumValues;     // Enumeration type only
};

struct DbcDatabase {
    QList<DbcNode> nodes;
    QVector<DbcMessage> messages;
    QList<DbcValueTable> valueTables;
    QList<DbcAttributeDef> attributes;

    void clear()
    {
        nodes.clear();
        messages.clear();
        valueTables.clear();
        attributes.clear();
    }
};

// Returns the Least Significant Bit (LSB) position of a signal.
// For Intel (little-endian): startBit is already the LSB.
// For Motorola (big-endian): startBit is the MSB — traverse forward to find the LSB.
inline int dbcSignalLsb(const DbcSignal& sig)
{
    const bool motorola = sig.byteOrder.compare("Motorola", Qt::CaseInsensitive) == 0;
    if (!motorola || sig.bitLength <= 1) {
        return sig.startBit;
    }
    int cur = sig.startBit;
    for (int i = 1; i < sig.bitLength; ++i) {
        if (cur % 8 == 0) { cur += 15; } else { cur -= 1; }
    }
    return cur;
}

// Returns the DBC startBit (MSB for Motorola, LSB for Intel) given the LSB position.
inline int dbcStartBitFromLsb(const DbcSignal& sig, int lsb)
{
    const bool motorola = sig.byteOrder.compare("Motorola", Qt::CaseInsensitive) == 0;
    if (!motorola || sig.bitLength <= 1) {
        return lsb;
    }
    int cur = lsb;
    for (int i = 1; i < sig.bitLength; ++i) {
        if (cur % 8 == 7) { cur -= 15; } else { cur += 1; }
    }
    return cur;
}
