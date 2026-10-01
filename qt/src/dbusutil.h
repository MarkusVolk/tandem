/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QDBusArgument>
#include <QVariant>

namespace Tandem
{

inline QVariant demarshal(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<QDBusVariant>()) {
        return demarshal(value.value<QDBusVariant>().variant());
    }
    if (value.userType() != qMetaTypeId<QDBusArgument>()) {
        return value;
    }
    const QDBusArgument arg = value.value<QDBusArgument>();
    switch (arg.currentType()) {
    case QDBusArgument::MapType: {
        QVariantMap map;
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            QVariant key;
            QVariant val;
            arg >> key >> val;
            arg.endMapEntry();
            map.insert(key.toString(), demarshal(val));
        }
        arg.endMap();
        return map;
    }
    case QDBusArgument::ArrayType: {
        if (arg.currentSignature() == QLatin1String("ay")) {
            QByteArray bytes;
            arg >> bytes;
            return bytes;
        }
        QVariantList list;
        arg.beginArray();
        while (!arg.atEnd()) {
            QVariant item;
            arg >> item;
            list.append(demarshal(item));
        }
        arg.endArray();
        return list;
    }
    case QDBusArgument::StructureType: {
        QVariantList list;
        arg.beginStructure();
        while (!arg.atEnd()) {
            QVariant item;
            arg >> item;
            list.append(demarshal(item));
        }
        arg.endStructure();
        return list;
    }
    default:
        return arg.asVariant();
    }
}

inline QVariantMap demarshalMap(const QVariant &value)
{
    return demarshal(value).toMap();
}

}
