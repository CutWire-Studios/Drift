#include "app/surfaces/remote/QrCode.h"

#include <array>
#include <vector>

namespace drift {

namespace {

struct VersionInfo {
    int dataCodewords;
    int eccPerBlock;
    int blocks;
    int totalCodewords;
    int align[3];
};

// Error correction level M.
const VersionInfo kVersions[] = {
    {0, 0, 0, 0, {0, 0, 0}},
    {16, 10, 1, 26, {0, 0, 0}},
    {28, 16, 1, 44, {6, 18, 0}},
    {44, 26, 1, 70, {6, 22, 0}},
    {64, 18, 2, 100, {6, 26, 0}},
    {86, 24, 2, 134, {6, 30, 0}},
    {108, 16, 4, 172, {6, 34, 0}},
    {124, 18, 4, 196, {6, 22, 38}},
    {154, 22, 4, 242, {6, 24, 42}},
    {182, 22, 5, 292, {6, 26, 46}},
    {216, 26, 5, 346, {6, 28, 50}},
};
constexpr int kMaxVersion = 10;

std::vector<quint8> reedSolomon(const std::vector<quint8> &data, int degree)
{
    // Generator polynomial of the given degree over GF(256) with 0x11D.
    std::vector<quint8> generator(degree, 0);
    generator.back() = 1;
    int root = 1;
    auto multiply = [](int x, int y) {
        int z = 0;
        for (int i = 7; i >= 0; --i) {
            z = (z << 1) ^ ((z >> 7) * 0x11D);
            z ^= ((y >> i) & 1) * x;
        }
        return z;
    };
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            generator[j] = static_cast<quint8>(multiply(generator[j], root));
            if (j + 1 < degree)
                generator[j] ^= generator[j + 1];
        }
        root = multiply(root, 0x02);
    }
    std::vector<quint8> result(degree, 0);
    for (quint8 b : data) {
        const int factor = b ^ result.front();
        result.erase(result.begin());
        result.push_back(0);
        for (int i = 0; i < degree; ++i)
            result[i] ^= static_cast<quint8>(multiply(generator[i], factor));
    }
    return result;
}

class Builder {
public:
    Builder(int version) : m_version(version), m_size(version * 4 + 17)
    {
        m_modules.assign(m_size * m_size, false);
        m_function.assign(m_size * m_size, false);
        drawFunctionPatterns();
    }

    void placeData(const std::vector<quint8> &codewords)
    {
        int i = 0;
        const int bits = static_cast<int>(codewords.size()) * 8;
        for (int right = m_size - 1; right >= 1; right -= 2) {
            if (right == 6)
                right = 5;
            for (int vert = 0; vert < m_size; ++vert) {
                for (int j = 0; j < 2; ++j) {
                    const int x = right - j;
                    const bool upward = ((right + 1) & 2) == 0;
                    const int y = upward ? m_size - 1 - vert : vert;
                    if (!m_function[y * m_size + x] && i < bits) {
                        m_modules[y * m_size + x] = ((codewords[i >> 3] >> (7 - (i & 7))) & 1) != 0;
                        ++i;
                    }
                }
            }
        }
    }

    // Picks the mask with the lowest penalty and returns the finished modules.
    QList<bool> finish()
    {
        int best = 0;
        long bestPenalty = -1;
        for (int mask = 0; mask < 8; ++mask) {
            applyMask(mask);
            drawFormat(mask);
            const long penalty = penaltyScore();
            if (bestPenalty < 0 || penalty < bestPenalty) {
                best = mask;
                bestPenalty = penalty;
            }
            applyMask(mask);
        }
        applyMask(best);
        drawFormat(best);
        QList<bool> out;
        out.reserve(m_size * m_size);
        for (bool b : m_modules)
            out.append(b);
        return out;
    }

private:
    void set(int x, int y, bool dark, bool function = true)
    {
        m_modules[y * m_size + x] = dark;
        if (function)
            m_function[y * m_size + x] = true;
    }

    void drawFinder(int cx, int cy)
    {
        for (int dy = -4; dy <= 4; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                const int x = cx + dx;
                const int y = cy + dy;
                if (x < 0 || x >= m_size || y < 0 || y >= m_size)
                    continue;
                const int dist = std::max(std::abs(dx), std::abs(dy));
                set(x, y, dist != 2 && dist != 4);
            }
        }
    }

    void drawAlignment(int cx, int cy)
    {
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx)
                set(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
        }
    }

    void drawFunctionPatterns()
    {
        for (int i = 0; i < m_size; ++i) {
            set(6, i, i % 2 == 0);
            set(i, 6, i % 2 == 0);
        }
        drawFinder(3, 3);
        drawFinder(m_size - 4, 3);
        drawFinder(3, m_size - 4);

        std::vector<int> positions;
        for (int p : kVersions[m_version].align) {
            if (p != 0)
                positions.push_back(p);
        }
        for (size_t a = 0; a < positions.size(); ++a) {
            for (size_t b = 0; b < positions.size(); ++b) {
                const bool corner = (a == 0 && b == 0) || (a == 0 && b == positions.size() - 1)
                                    || (a == positions.size() - 1 && b == 0);
                if (!corner)
                    drawAlignment(positions[a], positions[b]);
            }
        }
        drawFormat(0);
        drawVersion();
    }

    void drawFormat(int mask)
    {
        // ECC level M is 0b00.
        const int data = mask;
        int rem = data;
        for (int i = 0; i < 10; ++i)
            rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        const int bits = ((data << 10) | rem) ^ 0x5412;
        auto bit = [bits](int i) { return ((bits >> i) & 1) != 0; };
        for (int i = 0; i <= 5; ++i)
            set(8, i, bit(i));
        set(8, 7, bit(6));
        set(8, 8, bit(7));
        set(7, 8, bit(8));
        for (int i = 9; i < 15; ++i)
            set(14 - i, 8, bit(i));
        for (int i = 0; i < 8; ++i)
            set(m_size - 1 - i, 8, bit(i));
        for (int i = 8; i < 15; ++i)
            set(8, m_size - 15 + i, bit(i));
        set(8, m_size - 8, true);
    }

    void drawVersion()
    {
        if (m_version < 7)
            return;
        int rem = m_version;
        for (int i = 0; i < 12; ++i)
            rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        const int bits = (m_version << 12) | rem;
        for (int i = 0; i < 18; ++i) {
            const bool bit = ((bits >> i) & 1) != 0;
            const int a = m_size - 11 + i % 3;
            const int b = i / 3;
            set(a, b, bit);
            set(b, a, bit);
        }
    }

    void applyMask(int mask)
    {
        for (int y = 0; y < m_size; ++y) {
            for (int x = 0; x < m_size; ++x) {
                bool invert = false;
                switch (mask) {
                case 0: invert = (x + y) % 2 == 0; break;
                case 1: invert = y % 2 == 0; break;
                case 2: invert = x % 3 == 0; break;
                case 3: invert = (x + y) % 3 == 0; break;
                case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                case 5: invert = x * y % 2 + x * y % 3 == 0; break;
                case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
                case 7: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                }
                if (invert && !m_function[y * m_size + x])
                    m_modules[y * m_size + x] = !m_modules[y * m_size + x];
            }
        }
    }

    bool at(int x, int y) const { return m_modules[y * m_size + x]; }

    long penaltyScore() const
    {
        long result = 0;
        // Runs of one colour, and finder-like patterns, along rows then columns.
        for (int pass = 0; pass < 2; ++pass) {
            for (int a = 0; a < m_size; ++a) {
                int run = 1;
                std::array<int, 7> history{};
                auto get = [&](int b) { return pass == 0 ? at(b, a) : at(a, b); };
                for (int b = 1; b < m_size; ++b) {
                    if (get(b) == get(b - 1)) {
                        ++run;
                        if (run == 5)
                            result += 3;
                        else if (run > 5)
                            ++result;
                    } else {
                        run = 1;
                    }
                }
                // 1:1:3:1:1 with four light modules on a side.
                std::vector<bool> line;
                for (int b = 0; b < m_size; ++b)
                    line.push_back(get(b));
                static const bool pa[] = {true, false, true, true, true, false, true, false, false, false, false};
                static const bool pb[] = {false, false, false, false, true, false, true, true, true, false, true};
                for (int b = 0; b + 11 <= m_size; ++b) {
                    bool ma = true;
                    bool mb = true;
                    for (int k = 0; k < 11; ++k) {
                        ma = ma && line[b + k] == pa[k];
                        mb = mb && line[b + k] == pb[k];
                    }
                    if (ma || mb)
                        result += 40;
                }
                (void)history;
            }
        }
        for (int y = 0; y + 1 < m_size; ++y) {
            for (int x = 0; x + 1 < m_size; ++x) {
                const bool c = at(x, y);
                if (c == at(x + 1, y) && c == at(x, y + 1) && c == at(x + 1, y + 1))
                    result += 3;
            }
        }
        long dark = 0;
        for (bool b : m_modules)
            dark += b ? 1 : 0;
        const long total = static_cast<long>(m_size) * m_size;
        const long k = (std::labs(dark * 20 - total * 10) + total - 1) / total - 1;
        result += k * 10;
        return result;
    }

    int m_version;
    int m_size;
    std::vector<bool> m_modules;
    std::vector<bool> m_function;
};

} // namespace

QrCode QrCode::encode(const QByteArray &data)
{
    int version = 0;
    for (int v = 1; v <= kMaxVersion; ++v) {
        const int countBits = v < 10 ? 8 : 16;
        if (4 + countBits + data.size() * 8 <= kVersions[v].dataCodewords * 8) {
            version = v;
            break;
        }
    }
    QrCode code;
    if (version == 0)
        return code;

    std::vector<bool> bits;
    auto append = [&bits](unsigned value, int count) {
        for (int i = count - 1; i >= 0; --i)
            bits.push_back(((value >> i) & 1) != 0);
    };
    append(0x4, 4);
    append(static_cast<unsigned>(data.size()), version < 10 ? 8 : 16);
    for (char c : data)
        append(static_cast<quint8>(c), 8);

    const VersionInfo &info = kVersions[version];
    const int capacity = info.dataCodewords * 8;
    append(0, std::min(4, capacity - static_cast<int>(bits.size())));
    append(0, (8 - static_cast<int>(bits.size()) % 8) % 8);
    for (int pad = 0xEC; static_cast<int>(bits.size()) < capacity; pad ^= 0xEC ^ 0x11)
        append(static_cast<unsigned>(pad), 8);

    std::vector<quint8> codewords(bits.size() / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i)
        codewords[i >> 3] |= static_cast<quint8>(bits[i] ? 1 << (7 - (i & 7)) : 0);

    // Blocks: the short ones first, the long ones (one more data byte) after.
    const int shortBlocks = info.blocks - info.dataCodewords % info.blocks;
    const int shortLength = info.dataCodewords / info.blocks;
    std::vector<std::vector<quint8>> dataBlocks;
    std::vector<std::vector<quint8>> eccBlocks;
    int offset = 0;
    for (int b = 0; b < info.blocks; ++b) {
        const int length = shortLength + (b < shortBlocks ? 0 : 1);
        std::vector<quint8> block(codewords.begin() + offset, codewords.begin() + offset + length);
        offset += length;
        eccBlocks.push_back(reedSolomon(block, info.eccPerBlock));
        dataBlocks.push_back(std::move(block));
    }
    std::vector<quint8> interleaved;
    for (int i = 0; i < shortLength + 1; ++i) {
        for (const auto &block : dataBlocks) {
            if (i < static_cast<int>(block.size()))
                interleaved.push_back(block[i]);
        }
    }
    for (int i = 0; i < info.eccPerBlock; ++i) {
        for (const auto &block : eccBlocks)
            interleaved.push_back(block[i]);
    }

    Builder builder(version);
    builder.placeData(interleaved);
    code.m_version = version;
    code.m_size = version * 4 + 17;
    code.m_modules = builder.finish();
    return code;
}

} // namespace drift
