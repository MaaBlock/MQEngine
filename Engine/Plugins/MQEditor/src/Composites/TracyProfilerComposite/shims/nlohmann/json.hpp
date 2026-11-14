#pragma once

#include <string>
#include <vector>
#include <initializer_list>
#include <utility>
#include <sstream>
#include <type_traits>

namespace nlohmann {

class json {
public:
    json() = default;
    json(std::initializer_list<json>) {}
    template<typename T>
    json(const T&) {}

    static json array() { return json{}; }
    static json object() { return json{}; }
    static json parse(const std::string&) { return json{}; }
    template<typename It>
    static json parse(It, It) { return json{}; }
    template<typename FilePtr>
    static json parse(FilePtr) { return json{}; }

    template<typename T>
    json& operator[](const T&) {
        static json dummy;
        return dummy;
    }
    template<typename T>
    const json& operator[](const T&) const {
        static const json dummy;
        return dummy;
    }

    template<typename T>
    json& operator=(const T&) { return *this; }
    json& operator=(std::initializer_list<json>) { return *this; }

    void push_back(const json&) {}
    void push_back(std::initializer_list<json>) {}
    template<typename T>
    void push_back(T&&) {}

    json& back() {
        static json dummy;
        return dummy;
    }
    const json& back() const {
        static const json dummy;
        return dummy;
    }

    template<typename T>
    bool contains(const T&) const { return false; }
    bool is_array() const { return false; }
    bool is_string() const { return false; }
    bool empty() const { return true; }
    size_t size() const { return 0; }

    std::string dump(int = -1) const { return "{}"; }

    template<typename T>
    T get() const { return T{}; }

    template<typename T>
    decltype(auto) get_ref() const {
        using Bare = std::remove_cvref_t<T>;
        static Bare dummy{};
        return static_cast<T>(dummy);
    }

    json* begin() { return this; }
    json* end() { return this; }
    const json* begin() const { return this; }
    const json* end() const { return this; }

    struct exception : public std::exception {
        const char* what() const noexcept override { return "json"; }
    };
};

} // namespace nlohmann
