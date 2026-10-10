#pragma once
#include <QBuffer>
#include <QDataStream>
#include <QImage>
#include <QMap>
#include <zlib.h>

// GIO's thumbnail verifier only reads uncompressed PNG tEXt chunks.
// QImage::setText may compress long URIs into zTXt, which GIO rejects.
inline QByteArray thumbnailPng(const QImage &image, const QMap<QString, QString> &metadata) {
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    QByteArray chunks;
    QDataStream stream(&chunks, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    for (auto entry = metadata.cbegin(); entry != metadata.cend(); ++entry) {
        QByteArray text = entry.key().toLatin1() + '\0' + entry.value().toUtf8();
        QByteArray body = QByteArray("tEXt") + text;
        stream << quint32(text.size());
        stream.writeRawData(body.constData(), body.size());
        stream << quint32(crc32(0, reinterpret_cast<const Bytef *>(body.constData()), body.size()));
    }
    // PNG signature + the mandatory 13-byte IHDR chunk. Put metadata together
    // before image data so GIO can validate without reading the entire image.
    png.insert(33, chunks);
    return png;
}
