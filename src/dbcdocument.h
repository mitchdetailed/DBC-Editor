#pragma once

#include "dbcmodel.h"

#include <QString>

class DbcDocument {
public:
    static bool loadFromFile(const QString& filePath, DbcDatabase& outDatabase, QString& error);
    static bool saveToFile(const QString& filePath, const DbcDatabase& database, QString& error);
};
