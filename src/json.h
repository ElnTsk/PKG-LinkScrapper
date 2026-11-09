#pragma once

#include <map>
#include <string>
#include <vector>

struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string string;
  std::vector<JsonValue> array;
  std::map<std::string, JsonValue> object;

  bool isNull() const { return type == Type::Null; }
  bool isArray() const { return type == Type::Array; }
  bool isObject() const { return type == Type::Object; }
  bool isString() const { return type == Type::String; }

  const JsonValue* find(const std::string& key) const;
  bool asBool(bool fallback) const;
  double asDouble(double fallback) const;
  int asInt(int fallback) const;
  std::string asString(const std::string& fallback) const;
  std::vector<std::string> asStringList() const;
};

bool parseJson(const std::string& text, JsonValue& out, std::string& error);
std::string jsonEscape(const std::string& value);
