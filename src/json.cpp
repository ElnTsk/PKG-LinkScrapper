#include "json.h"

#include <cstdlib>
#include <cstring>

namespace {

struct Parser {
  const std::string& text;
  size_t pos = 0;
  std::string error;

  void skipSpace() {
    while (pos < text.size()) {
      char c = text[pos];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos;
      } else if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '/') {
        while (pos < text.size() && text[pos] != '\n') ++pos;
      } else {
        break;
      }
    }
  }

  bool fail(const std::string& message) {
    if (error.empty()) error = message + " at offset " + std::to_string(pos);
    return false;
  }

  bool parseValue(JsonValue& out);

  bool parseString(std::string& out) {
    if (pos >= text.size() || text[pos] != '"') return fail("expected string");
    ++pos;
    out.clear();
    while (pos < text.size()) {
      char c = text[pos++];
      if (c == '"') return true;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos >= text.size()) return fail("truncated escape");
      char escape = text[pos++];
      switch (escape) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case '/': out.push_back('/'); break;
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case 'u': {
          if (pos + 4 > text.size()) return fail("short unicode escape");
          unsigned code = static_cast<unsigned>(std::strtoul(text.substr(pos, 4).c_str(), nullptr, 16));
          pos += 4;
          if (code < 0x80) {
            out.push_back(static_cast<char>(code));
          } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          break;
        }
        default:
          return fail("bad escape");
      }
    }
    return fail("unterminated string");
  }
};

bool Parser::parseValue(JsonValue& out) {
  skipSpace();
  if (pos >= text.size()) return fail("unexpected end");

  char c = text[pos];
  if (c == '{') {
    ++pos;
    out.type = JsonValue::Type::Object;
    skipSpace();
    if (pos < text.size() && text[pos] == '}') {
      ++pos;
      return true;
    }
    for (;;) {
      skipSpace();
      std::string key;
      if (!parseString(key)) return false;
      skipSpace();
      if (pos >= text.size() || text[pos] != ':') return fail("expected colon");
      ++pos;
      JsonValue value;
      if (!parseValue(value)) return false;
      out.object[key] = std::move(value);
      skipSpace();
      if (pos < text.size() && text[pos] == ',') {
        ++pos;
        continue;
      }
      if (pos < text.size() && text[pos] == '}') {
        ++pos;
        return true;
      }
      return fail("expected comma or brace");
    }
  }

  if (c == '[') {
    ++pos;
    out.type = JsonValue::Type::Array;
    skipSpace();
    if (pos < text.size() && text[pos] == ']') {
      ++pos;
      return true;
    }
    for (;;) {
      JsonValue value;
      if (!parseValue(value)) return false;
      out.array.push_back(std::move(value));
      skipSpace();
      if (pos < text.size() && text[pos] == ',') {
        ++pos;
        continue;
      }
      if (pos < text.size() && text[pos] == ']') {
        ++pos;
        return true;
      }
      return fail("expected comma or bracket");
    }
  }

  if (c == '"') {
    std::string value;
    if (!parseString(value)) return false;
    out.type = JsonValue::Type::String;
    out.string = value;
    return true;
  }

  if (text.compare(pos, 4, "true") == 0) {
    pos += 4;
    out.type = JsonValue::Type::Bool;
    out.boolean = true;
    return true;
  }
  if (text.compare(pos, 5, "false") == 0) {
    pos += 5;
    out.type = JsonValue::Type::Bool;
    out.boolean = false;
    return true;
  }
  if (text.compare(pos, 4, "null") == 0) {
    pos += 4;
    out.type = JsonValue::Type::Null;
    return true;
  }

  size_t start = pos;
  while (pos < text.size()) {
    char ch = text[pos];
    if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E') {
      ++pos;
    } else {
      break;
    }
  }
  if (start == pos) return fail("unexpected token");
  out.type = JsonValue::Type::Number;
  out.number = std::strtod(text.substr(start, pos - start).c_str(), nullptr);
  return true;
}

}  // namespace

const JsonValue* JsonValue::find(const std::string& key) const {
  auto found = object.find(key);
  return found == object.end() ? nullptr : &found->second;
}

bool JsonValue::asBool(bool fallback) const {
  if (type == Type::Bool) return boolean;
  if (type == Type::Number) return number != 0;
  if (type == Type::String) return string == "true" || string == "1";
  return fallback;
}

double JsonValue::asDouble(double fallback) const {
  if (type == Type::Number) return number;
  if (type == Type::String) return std::strtod(string.c_str(), nullptr);
  return fallback;
}

int JsonValue::asInt(int fallback) const {
  if (type == Type::Number) return static_cast<int>(number);
  if (type == Type::String) return std::atoi(string.c_str());
  return fallback;
}

std::string JsonValue::asString(const std::string& fallback) const {
  if (type == Type::String) return string;
  return fallback;
}

std::vector<std::string> JsonValue::asStringList() const {
  std::vector<std::string> out;
  if (type == Type::Array) {
    for (const JsonValue& item : array) {
      if (item.type == Type::String) out.push_back(item.string);
    }
  } else if (type == Type::String) {
    out.push_back(string);
  }
  return out;
}

bool parseJson(const std::string& text, JsonValue& out, std::string& error) {
  Parser parser{text, 0, ""};
  JsonValue parsed;
  if (!parser.parseValue(parsed)) {
    error = parser.error;
    out = JsonValue{};
    return false;
  }
  parser.skipSpace();
  if (parser.pos != text.size()) {
    error = "trailing characters at offset " + std::to_string(parser.pos);
    out = JsonValue{};
    return false;
  }
  out = std::move(parsed);
  return true;
}

std::string jsonEscape(const std::string& value) {
  std::string out = "\"";
  for (unsigned char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          out += buffer;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out += "\"";
  return out;
}
