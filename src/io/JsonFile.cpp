#include "io/JsonFile.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>

namespace reconstruct_one_frame {

namespace {

std::regex keyValueRegex(const std::string& key, const std::string& valuePattern)
{
    const std::string escapedKey = std::regex_replace(key, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\\$&)");
    return std::regex("\"" + escapedKey + R"("\s*:\s*)" + valuePattern, std::regex::icase);
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
    std::smatch match;
    if (!std::regex_search(rawJson_, match, keyValueRegex(key, R"(\[([^\]]*)\])"))) {
        return false;
    }

    std::vector<int> parsed;
    const std::string body = match[1].str();
    const std::regex numberRegex(R"(-?\d+)");
    auto begin = std::sregex_iterator(body.begin(), body.end(), numberRegex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        parsed.push_back(std::stoi((*it).str()));
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

} // namespace reconstruct_one_frame
