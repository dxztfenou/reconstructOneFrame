#pragma once

#include "reconstruct_one_frame/reconstructInterface.h"

#include <map>
#include <string>
#include <vector>

namespace reconstruct_one_frame {

class JsonObject {
public:
    explicit JsonObject(std::string rawJson);

    [[nodiscard]] bool hasKey(const std::string& key) const;
    [[nodiscard]] bool getInt(const std::string& key, int& value) const;
    [[nodiscard]] bool getDouble(const std::string& key, double& value) const;
    [[nodiscard]] bool getBool(const std::string& key, bool& value) const;
    [[nodiscard]] bool getString(const std::string& key, std::string& value) const;
    [[nodiscard]] bool getIntArray(const std::string& key, std::vector<int>& value) const;
    [[nodiscard]] const std::string& raw() const noexcept { return rawJson_; }

private:
    std::string rawJson_;
};

Status readTextFile(const std::string& path, std::string& contents);
Status loadJsonObjectFromFile(const std::string& path, JsonObject& object);
bool looksLikeJsonObject(const std::string& text);

} // namespace reconstruct_one_frame
