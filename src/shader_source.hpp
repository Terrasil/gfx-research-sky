#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

// GLSL has no standard #include. Expand project-local includes before compilation.
inline std::string shaderSource(const std::filesystem::path& path, int depth = 0) {
    if (depth > 16) throw std::runtime_error("Shader include recursion: " + path.string());
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Unable to read shader: " + path.string());
    std::ostringstream output;
    for (std::string line; std::getline(input, line);) {
        const auto begin = line.find("#include \"");
        if (begin == std::string::npos) { output << line << '\n'; continue; }
        const auto first = begin + 10;
        const auto last = line.find('"', first);
        if (last == std::string::npos) throw std::runtime_error("Malformed include in " + path.string());
        output << shaderSource(path.parent_path() / line.substr(first, last-first), depth+1);
    }
    return output.str();
}
