#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lsm {
    void WriteSSTable(
        const std::string& path,
        const std::vector<std::pair<std::string, std::optional<std::string>>>& rows);

    
}