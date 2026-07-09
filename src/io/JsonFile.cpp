#include "io/JsonFile.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <vector>

namespace reconstruct_one_frame {

namespace {

std::regex keyValueRegex(const std::string& key, const std::string& valuePattern)
{
    const std::string escapedKey = std::regex_replace(key, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\\$&)");
    return std::regex("\"" + escapedKey + R"("\s*:\s*)" + valuePattern, std::regex::icase);
}

bool extractArrayBody(const std::string& json, const std::string& key, std::string& body)
{
    const std::string escapedKey = std::regex_replace(key, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\\$&)");
    std::smatch match;
    if (!std::regex_search(json, match, std::regex("\"" + escapedKey + R"("\s*:\s*\[)"))) {
        return false;
    }

    std::size_t pos = static_cast<std::size_t>(match.position(0) + match.length(0));
    int depth = 1;
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = pos; i < json.size(); ++i) {
        const char c = json[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '[') {
            ++depth;
        } else if (c == ']') {
            --depth;
            if (depth == 0) {
                body = json.substr(pos, i - pos);
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> extractObjectsFromArrayBody(const std::string& body)
{
    std::vector<std::string> objects;
    bool inString = false;
    bool escaped = false;
    int depth = 0;
    std::size_t start = std::string::npos;

    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            if (depth == 0) {
                start = i;
            }
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                objects.push_back(body.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return objects;
}

} // namespace

JsonObject::JsonObject(std::string rawJson)
    : rawJson_(std::move(rawJson))
{
}

bool JsonObject::hasKey(const std::string& key) const
{
    return std::regex_search(rawJson_, std::regex("\"" + key + R"("\s*:)", std::regex::icase));
}

bool JsonObject::getInt(const std::string& key, int& value) const
{
    std::smatch match;
    if (!std::regex_search(rawJson_, match, keyValueRegex(key, R"((-?\d+))"))) {
        return false;
    }
    value = std::stoi(match[1].str());
    return true;
}

bool JsonObject::getDouble(const std::string& key, double& value) const
{
    std::smatch match;
    if (!std::regex_search(rawJson_, match, keyValueRegex(key, R"((-?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?))"))) {
        return false;
    }
    value = std::stod(match[1].str());
    return true;
}

bool JsonObject::getBool(const std::string& key, bool& value) const
{
    std::smatch match;
    if (!std::regex_search(rawJson_, match, keyValueRegex(key, R"((true|false))"))) {
        return false;
    }
    value = match[1].str() == "true" || match[1].str() == "TRUE";
    return true;
}

bool JsonObject::getString(const std::string& key, std::string& value) const
{
    std::smatch match;
    if (!std::regex_search(rawJson_, match, keyValueRegex(key, R"json("([^"]*)")json"))) {
        return false;
    }
    value = match[1].str();
    return true;
}

bool JsonObject::getIntArray(const std::string& key, std::vector<int>& value) const
{
    std::string body;
    if (!extractArrayBody(rawJson_, key, body)) {
        return false;
    }

    std::vector<int> parsed;
    const std::regex numberRegex(R"(-?\d+)");
    auto begin = std::sregex_iterator(body.begin(), body.end(), numberRegex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        parsed.push_back(std::stoi((*it).str()));
    }
    value = std::move(parsed);
    return true;
}

bool JsonObject::getDoubleArray(const std::string& key, std::vector<double>& value) const
{
    std::string body;
    if (!extractArrayBody(rawJson_, key, body)) {
        return false;
    }

    std::vector<double> parsed;
    const std::regex numberRegex(R"(-?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)");
    auto begin = std::sregex_iterator(body.begin(), body.end(), numberRegex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        parsed.push_back(std::stod((*it).str()));
    }
    value = std::move(parsed);
    return true;
}

bool JsonObject::getObjectArray(const std::string& key, std::vector<JsonObject>& value) const
{
    std::string body;
    if (!extractArrayBody(rawJson_, key, body)) {
        return false;
    }

    std::vector<JsonObject> parsed;
    for (const std::string& objectText : extractObjectsFromArrayBody(body)) {
        parsed.emplace_back(objectText);
    }
    value = std::move(parsed);
    return true;
}

Status readTextFile(const std::string& path, std::string& contents)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {StatusCode::ConfigMissing, "JsonFile", "file not found: " + path};
    }
    contents.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return {};
}

bool looksLikeJsonObject(const std::string& text)
{
    auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) { return std::isspace(c) != 0; });
    if (first == text.end() || last == text.rend()) {
        return false;
    }
    return *first == '{' && *last == '}';
}

Status loadJsonObjectFromFile(const std::string& path, JsonObject& object)
{
    std::string text;
    Status status = readTextFile(path, text);
    if (!status.ok()) {
        return status;
    }
    if (!looksLikeJsonObject(text)) {
        return {StatusCode::ConfigParseFailed, "JsonFile", "invalid JSON object: " + path};
    }
    object = JsonObject(std::move(text));
    return {};
}

std::string statusCodeOverrideModule(Status status, const std::string& module)
{
    status.module = module;
    return status.message;
}

} // namespace reconstruct_one_frame
