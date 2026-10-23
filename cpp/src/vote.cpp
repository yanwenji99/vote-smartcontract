#include "vote.hpp"

#include <cstdint>
#include <limits>

namespace votesc {
namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

// 判断字符是否为 ASCII 数字（不使用 std::isdigit，避免受 locale 影响）。
bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

// 把一个 Unicode 码点按 UTF-8 编码追加到 out。
void appendCodePoint(std::string& out, std::uint32_t codePoint) {
    if (codePoint <= 0x7F) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

// 按 Go encoding/json 的默认规则转义字符串。
//
// 与 Go 的差异：非法 UTF-8 字节会被原样输出，而 Go 会替换为 U+FFFD。
// 正常 UTF-8 用户名（包括中文）不受影响。
void appendEscapedString(std::string& out, const std::string& value) {
    out.push_back('"');
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            // Go 默认开启 HTML 转义，因此 <、>、& 也会被转成 \uXXXX。
            case '<':  out += "\\u003c"; break;
            case '>':  out += "\\u003e"; break;
            case '&':  out += "\\u0026"; break;
            default:
                if (c < 0x20) {
                    // 其他控制字符：\u00XX（小写十六进制）。
                    out += "\\u00";
                    out.push_back(kHexDigits[c >> 4]);
                    out.push_back(kHexDigits[c & 0x0F]);
                } else if (c == 0xE2 && i + 2 < value.size() &&
                           static_cast<unsigned char>(value[i + 1]) == 0x80 &&
                           (static_cast<unsigned char>(value[i + 2]) == 0xA8 ||
                            static_cast<unsigned char>(value[i + 2]) == 0xA9)) {
                    // U+2028 / U+2029 在 JavaScript 中是行分隔符，Go 会转义它们。
                    out += (static_cast<unsigned char>(value[i + 2]) == 0xA8) ? "\\u2028" : "\\u2029";
                    i += 2;
                } else {
                    // 普通 ASCII 和其余 UTF-8 字节原样输出。
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

// 带 Vote schema 的 JSON 读取器。
//
// 只实现账本值需要的语法子集：对象、字符串、整数、布尔、null、数组，
// 以及跳过未知字段所需的最小数值解析。遇到错误时抛出带偏移的 JsonError。
class JsonReader {
public:
    explicit JsonReader(const std::string& text) : text_(text) {}

    Vote parseVote() {
        Vote vote;

        skipWhitespace();
        expect('{');
        skipWhitespace();

        if (peek() == '}') {
            take();
        } else {
            while (true) {
                skipWhitespace();
                const std::string key = parseString();
                skipWhitespace();
                expect(':');
                skipWhitespace();

                if (key == "id") {
                    vote.id = parseInt();
                } else if (key == "username") {
                    vote.username = parseString();
                } else if (key == "votes") {
                    vote.votes = parseInt();
                } else {
                    skipValue(); // 未知字段：按 JSON 语法跳过
                }

                skipWhitespace();
                const char next = take();
                if (next == '}') {
                    break;
                }
                if (next != ',') {
                    fail("expected ',' or '}'");
                }
            }
        }

        skipWhitespace();
        if (pos_ != text_.size()) {
            fail("unexpected trailing characters");
        }
        return vote;
    }

private:
    void skipWhitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    char peek() const {
        return pos_ < text_.size() ? text_[pos_] : '\0';
    }

    char take() {
        if (pos_ >= text_.size()) {
            fail("unexpected end of input");
        }
        return text_[pos_++];
    }

    void expect(char expected) {
        const char actual = take();
        if (actual != expected) {
            fail(std::string("expected '") + expected + "'");
        }
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw JsonError(message, pos_);
    }

    // 解析 JSON 字符串（支持转义和 \uXXXX 代理对）。
    std::string parseString() {
        expect('"');

        std::string result;
        while (true) {
            if (pos_ >= text_.size()) {
                fail("unterminated string");
            }

            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') {
                break;
            }
            if (c < 0x20) {
                fail("unescaped control character in string");
            }
            if (c != '\\') {
                result.push_back(static_cast<char>(c));
                continue;
            }

            if (pos_ >= text_.size()) {
                fail("unterminated escape sequence");
            }
            const char escape = text_[pos_++];
            switch (escape) {
                case '"':  result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/':  result.push_back('/'); break;
                case 'b':  result.push_back('\b'); break;
                case 'f':  result.push_back('\f'); break;
                case 'n':  result.push_back('\n'); break;
                case 'r':  result.push_back('\r'); break;
                case 't':  result.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codePoint = parseHex4();
                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                        // 高代理项：后面必须紧跟 \uDC00-\uDFFF 才能组成一个字符。
                        if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                            fail("unpaired high surrogate");
                        }
                        pos_ += 2;
                        const std::uint32_t low = parseHex4();
                        if (low < 0xDC00 || low > 0xDFFF) {
                            fail("invalid low surrogate");
                        }
                        codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                    } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                        fail("unpaired low surrogate");
                    }
                    appendCodePoint(result, codePoint);
                    break;
                }
                default:
                    fail("invalid escape sequence");
            }
        }
        return result;
    }

    std::uint32_t parseHex4() {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos_ >= text_.size()) {
                fail("incomplete \\u escape");
            }
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                fail("invalid hex digit in \\u escape");
            }
        }
        return value;
    }

    // 解析 JSON 整数，并要求结果落在 int 范围内。
    int parseInt() {
        const bool negative = (peek() == '-');
        if (negative) {
            ++pos_;
        }

        if (pos_ >= text_.size() || !isDigit(text_[pos_])) {
            fail("expected integer");
        }
        // JSON 不允许前导零（"01" 非法，但 "0" 合法）。
        if (text_[pos_] == '0' && pos_ + 1 < text_.size() && isDigit(text_[pos_ + 1])) {
            fail("leading zeros are not allowed");
        }

        long long value = 0;
        while (pos_ < text_.size() && isDigit(text_[pos_])) {
            value = value * 10 + (text_[pos_] - '0');
            // 绝对值最多允许 INT_MAX + 1（用于表示 INT_MIN）。
            if (value > static_cast<long long>(std::numeric_limits<int>::max()) + 1) {
                fail("integer overflow");
            }
            ++pos_;
        }

        if (negative) {
            value = -value;
        }
        if (value < std::numeric_limits<int>::min() ||
            value > std::numeric_limits<int>::max()) {
            fail("integer overflow");
        }

        // 拒绝小数和指数形式；其他非法字符由调用方在检查分隔符时报错。
        const char next = peek();
        if (next == '.' || next == 'e' || next == 'E') {
            fail("expected integer");
        }

        return static_cast<int>(value);
    }

    // 跳过任意一个 JSON 值（用于未知字段）。
    void skipValue() {
        skipWhitespace();
        const char c = peek();
        if (c == '"') {
            parseString();
        } else if (c == '{') {
            skipObject();
        } else if (c == '[') {
            skipArray();
        } else if (c == 't') {
            skipLiteral("true");
        } else if (c == 'f') {
            skipLiteral("false");
        } else if (c == 'n') {
            skipLiteral("null");
        } else {
            skipNumber();
        }
    }

    void skipObject() {
        expect('{');
        skipWhitespace();
        if (peek() == '}') {
            take();
            return;
        }

        while (true) {
            skipWhitespace();
            parseString();
            skipWhitespace();
            expect(':');
            skipValue();
            skipWhitespace();

            const char c = take();
            if (c == '}') {
                break;
            }
            if (c != ',') {
                fail("expected ',' or '}'");
            }
        }
    }

    void skipArray() {
        expect('[');
        skipWhitespace();
        if (peek() == ']') {
            take();
            return;
        }

        while (true) {
            skipValue();
            skipWhitespace();

            const char c = take();
            if (c == ']') {
                break;
            }
            if (c != ',') {
                fail("expected ',' or ']'");
            }
        }
    }

    void skipLiteral(const char* literal) {
        for (const char* p = literal; *p != '\0'; ++p) {
            if (pos_ >= text_.size() || text_[pos_] != *p) {
                fail("invalid literal");
            }
            ++pos_;
        }
    }

    void skipNumber() {
        if (peek() == '-') {
            ++pos_;
        }

        if (pos_ >= text_.size()) {
            fail("invalid number");
        }
        if (text_[pos_] == '0') {
            ++pos_;
        } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
            while (pos_ < text_.size() && isDigit(text_[pos_])) {
                ++pos_;
            }
        } else {
            fail("invalid number");
        }

        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (pos_ >= text_.size() || !isDigit(text_[pos_])) {
                fail("invalid number");
            }
            while (pos_ < text_.size() && isDigit(text_[pos_])) {
                ++pos_;
            }
        }

        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            if (pos_ >= text_.size() || !isDigit(text_[pos_])) {
                fail("invalid number");
            }
            while (pos_ < text_.size() && isDigit(text_[pos_])) {
                ++pos_;
            }
        }
    }

    const std::string& text_;
    std::size_t pos_ = 0;
};

} // namespace

std::string voteToJson(const Vote& vote) {
    std::string out;
    out.reserve(48 + vote.username.size());
    out += "{\"id\":";
    out += std::to_string(vote.id);
    out += ",\"username\":";
    appendEscapedString(out, vote.username);
    out += ",\"votes\":";
    out += std::to_string(vote.votes);
    out += '}';
    return out;
}

Vote voteFromJson(const std::string& json) {
    JsonReader reader(json);
    return reader.parseVote();
}

} // namespace votesc
