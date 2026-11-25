#pragma once

#include <QByteArray>
#include <QList>

namespace drift {

// A QR code in byte mode with medium error correction, versions 1 to 10 (up to 213 bytes). It
// exists so the pairing screen can draw a code without a library.
class QrCode {
public:
    // An empty code when `data` does not fit.
    static QrCode encode(const QByteArray &data);

    bool isValid() const { return m_size > 0; }
    int size() const { return m_size; }
    bool dark(int x, int y) const { return m_modules.at(y * m_size + x); }
    int version() const { return m_version; }

private:
    int m_size = 0;
    int m_version = 0;
    QList<bool> m_modules;
};

} // namespace drift
