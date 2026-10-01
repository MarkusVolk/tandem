/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QDBusArgument>
#include <QDBusVariant>
#include <QStringList>
#include <QVariant>

namespace Tandem
{

QVariant demarshal(const QVariant &value);

inline QVariant readArgument(const QDBusArgument &arg)
{
    switch (arg.currentType()) {
    case QDBusArgument::VariantType: {
        QDBusVariant v;
        arg >> v;
        return demarshal(v.variant());
    }
    case QDBusArgument::ArrayType: {
        const QString signature = arg.currentSignature();
        if (signature == QLatin1String("ay")) {
            QByteArray bytes;
            arg >> bytes;
            return bytes;
        }
        if (signature == QLatin1String("as")) {
            QStringList strings;
            arg >> strings;
            return strings;
        }
        QVariantList list;
        arg.beginArray();
        while (!arg.atEnd()) {
            list.append(readArgument(arg));
        }
        arg.endArray();
        return list;
    }
    case QDBusArgument::StructureType: {
        QVariantList list;
        arg.beginStructure();
        while (!arg.atEnd()) {
            list.append(readArgument(arg));
        }
        arg.endStructure();
        return list;
    }
    case QDBusArgument::MapType: {
        QVariantMap map;
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            const QString key = readArgument(arg).toString();
            const QVariant val = readArgument(arg);
            arg.endMapEntry();
            map.insert(key, val);
        }
        arg.endMap();
        return map;
    }
    case QDBusArgument::BasicType:
    default:
        return demarshal(arg.asVariant());
    }
}

inline QVariant demarshal(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<QDBusVariant>()) {
        return demarshal(value.value<QDBusVariant>().variant());
    }
    if (value.userType() == qMetaTypeId<QDBusArgument>()) {
        return readArgument(value.value<QDBusArgument>());
    }
    if (value.typeId() == QMetaType::QVariantMap) {
        QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) {
            it.value() = demarshal(it.value());
        }
        return map;
    }
    return value;
}

inline QVariantMap demarshalMap(const QVariant &value)
{
    return demarshal(value).toMap();
}

}
