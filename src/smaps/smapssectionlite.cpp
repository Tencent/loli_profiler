#include "smaps/smapssectionlite.h"

#include <cctype>
#include <cstdlib>

namespace {

// True when the line starts a mapping entry:
// start-end perm offset dev inode (the Qt original used a QRegExp on
// [0-9a-z-] tokens; smaps fields are hex without the 0x prefix, so parsing
// the leading token shape directly is equivalent for real smaps text).
bool IsMappingLine(const std::string& line, uint64_t& start, uint64_t& end,
                   uint64_t& offset) {
    std::size_t i = 0;
    const std::size_t n = line.size();

    auto parseHex = [&](uint64_t& value, std::size_t& pos) {
        const std::size_t begin = pos;
        uint64_t v = 0;
        while (pos < n && std::isxdigit(static_cast<unsigned char>(line[pos]))) {
            const char c = line[pos];
            v = v * 16u + static_cast<uint64_t>(
                c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
            ++pos;
        }
        if (pos == begin)
            return false;
        value = v;
        return true;
    };

    if (!parseHex(start, i))
        return false;
    if (i >= n || line[i] != '-')
        return false;
    ++i;
    if (!parseHex(end, i))
        return false;

    // Exactly one blank-separated field (perms) before the offset.
    while (i < n && line[i] == ' ')
        ++i;
    while (i < n && line[i] != ' ')
        ++i;
    while (i < n && line[i] == ' ')
        ++i;
    if (!parseHex(offset, i))
        return false;
    return true;
}

} // namespace

void ParseSmapsText(const std::string& text,
                    std::unordered_map<std::string, SMapsSectionLite>& outSections,
                    SMapsSectionLite* total) {
    SMapsSectionLite* curSection = nullptr;
    std::size_t pos = 0;
    const std::size_t n = text.size();

    while (pos <= n) {
        // Read one line (handle \n and \r\n).
        std::size_t eol = text.find('\n', pos);
        std::size_t next = (eol == std::string::npos) ? std::string::npos : eol + 1;
        if (eol == std::string::npos)
            eol = n;
        std::size_t len = eol - pos;
        if (len > 0 && text[pos + len - 1] == '\r')
            --len;
        const std::string line = text.substr(pos, len);
        pos = next;
        if (next == std::string::npos && pos > n)
            break;

        if (line.empty())
            continue;

        uint64_t start = 0, end = 0, offset = 0;
        if (IsMappingLine(line, start, end, offset)) {
            // Library name = the remainder of the line after the inode
            // column, trimmed (QRegExp matchedLength parity). The mapping
            // line has 5 leading fields; the name follows.
            std::size_t nameStart = 0;
            int fields = 0;
            std::size_t i = 0;
            while (i < line.size() && fields < 5) {
                while (i < line.size() && line[i] == ' ')
                    ++i;
                while (i < line.size() && line[i] != ' ')
                    ++i;
                ++fields;
                nameStart = i;
            }
            std::string libName = line.substr(nameStart);
            // Trim whitespace.
            std::size_t b = libName.find_first_not_of(" \t");
            std::size_t e = libName.find_last_not_of(" \t");
            libName = (b == std::string::npos) ? std::string()
                                               : libName.substr(b, e - b + 1);

            if (!libName.empty() && libName[0] != '[') {
                // Demangle: keep only the basename (Qt original's Demangle).
                const std::size_t slash = libName.find_last_of('/');
                if (slash != std::string::npos && slash > 0)
                    libName = libName.substr(slash + 1);
            }
            if (libName.empty())
                libName = "anonymous";

            curSection = &outSections[libName];
            curSection->addrs_.emplace_back(start, end, offset);
        } else if (curSection != nullptr) {
            // Metric line: "Key:" value kB ...
            std::size_t i = 0;
            while (i < line.size() && line[i] == ' ')
                ++i;
            const std::size_t keyBegin = i;
            while (i < line.size() && line[i] != ' ')
                ++i;
            const std::string word = line.substr(keyBegin, i - keyBegin);
            while (i < line.size() && line[i] == ' ')
                ++i;
            const std::size_t valBegin = i;
            while (i < line.size() && line[i] != ' ')
                ++i;
            if (valBegin == i)
                continue;
            const unsigned long value = std::strtoul(line.substr(valBegin, i - valBegin).c_str(),
                                                     nullptr, 10);
            uint32_t* field = nullptr;
            if (word == "Size:")
                field = &curSection->virtual_;
            else if (word == "Rss:")
                field = &curSection->rss_;
            else if (word == "Pss:")
                field = &curSection->pss_;
            else if (word == "Shared_Clean:")
                field = &curSection->sharedClean_;
            else if (word == "Shared_Dirty:")
                field = &curSection->sharedDirty_;
            else if (word == "Private_Dirty:")
                field = &curSection->privateDirty_;
            else if (word == "Private_Clean:")
                field = &curSection->privateClean_;
            if (field != nullptr) {
                *field += static_cast<uint32_t>(value);
                if (total != nullptr) {
                    if (word == "Size:")
                        total->virtual_ += static_cast<uint32_t>(value);
                    else if (word == "Rss:")
                        total->rss_ += static_cast<uint32_t>(value);
                    else if (word == "Pss:")
                        total->pss_ += static_cast<uint32_t>(value);
                    else if (word == "Shared_Clean:")
                        total->sharedClean_ += static_cast<uint32_t>(value);
                    else if (word == "Shared_Dirty:")
                        total->sharedDirty_ += static_cast<uint32_t>(value);
                    else if (word == "Private_Dirty:")
                        total->privateDirty_ += static_cast<uint32_t>(value);
                    else if (word == "Private_Clean:")
                        total->privateClean_ += static_cast<uint32_t>(value);
                }
            }
        }
    }
}
