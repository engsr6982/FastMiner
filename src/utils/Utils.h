#pragma once
#include <fmt/format.h>
#include <iostream>
#include <ll/api/form/CustomForm.h>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>


using string = std::string;

namespace fm::utils {

template <typename T>
inline string join(std::vector<T> const& vec, string const& splitter = ", ") {
    if (vec.empty()) return "";
    return std::accumulate(
        std::next(vec.begin()),
        vec.end(),
        std::to_string(vec[0]),
        [splitter](const string& a, const T& b) -> string { return a + splitter + std::to_string(b); }
    );
}

template <typename T>
inline bool some(std::vector<T> const& vec, T const& it) {
    if (vec.empty()) return false;
    return std::find(vec.begin(), vec.end(), it) != vec.end();
}


} // namespace fm::utils