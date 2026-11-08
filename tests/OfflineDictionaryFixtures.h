#pragma once

#include "DictionaryFixtures.h"
#include <zlib.h>

inline QByteArray gzip_bytes(const QByteArray &data) {
    z_stream stream{};
    if (deflateInit2(&stream, 1, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    QByteArray output(compressBound(data.size()) + 64, '\0');
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = data.size();
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = output.size();
    const int result = deflate(&stream, Z_FINISH);
    output.resize(stream.total_out);
    deflateEnd(&stream);
    return result == Z_STREAM_END ? output : QByteArray{};
}

inline QByteArray offline_dictionary_data() {
    return kaikki_entry("Häuser", "plural of Haus", "de", "Haus")
        + kaikki_entry("Haus", QString::fromUtf8("дом"), "de", {}, house_forms())
        + kaikki_entry("house", QString::fromUtf8("дом"), "en")
        + kaikki_entry("Haus", "not German", "fr");
}
