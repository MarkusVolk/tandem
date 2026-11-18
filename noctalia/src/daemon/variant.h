#pragma once

#include <cstdint>
#include <gio/gio.h>
#include <string>
#include <string_view>
#include <vector>

namespace tandem {
  // A reference to a GVariant, for reading the a{sv} maps the daemon answers with.
  class Variant {
  public:
    Variant() = default;
    // Takes a reference; a floating variant is sunk.
    explicit Variant(GVariant* value);
    // Takes over the reference the caller holds.
    static Variant adopt(GVariant* value);
    Variant(const Variant& other);
    Variant(Variant&& other) noexcept;
    Variant& operator=(Variant other) noexcept;
    ~Variant();

    explicit operator bool() const noexcept { return m_value != nullptr; }
    GVariant* get() const noexcept { return m_value; }
    bool is(const GVariantType* type) const;

    // The value itself; variants are unboxed.
    std::string string() const;
    std::int64_t integer() const;
    bool boolean() const;
    double number() const;
    std::vector<std::string> strings() const;
    // The elements of an array or the fields of a tuple.
    std::vector<Variant> children() const;

    // Members of an a{sv} map; a missing key or another type yields the fallback.
    Variant lookup(std::string_view key) const;
    bool has(std::string_view key) const;
    std::string string(std::string_view key, std::string_view fallback = {}) const;
    std::int64_t integer(std::string_view key, std::int64_t fallback = 0) const;
    bool boolean(std::string_view key, bool fallback = false) const;
    double number(std::string_view key, double fallback = 0.0) const;
    std::vector<std::string> strings(std::string_view key) const;
    // aa{sv} members, such as the notifications or the SMS threads.
    std::vector<Variant> maps(std::string_view key) const;
    // An ay member, such as an attachment thumbnail.
    std::vector<std::uint8_t> bytes(std::string_view key) const;

  private:
    GVariant* m_value = nullptr;
  };

  // Builds an a{sv} map for CallPlugin and friends.
  class VariantMap {
  public:
    VariantMap();
    ~VariantMap();
    VariantMap(const VariantMap&) = delete;
    VariantMap& operator=(const VariantMap&) = delete;
    VariantMap& add(const char* key, GVariant* value);
    VariantMap& add(const char* key, const std::string& value);
    VariantMap& add(const char* key, const char* value);
    VariantMap& add(const char* key, bool value);
    VariantMap& add(const char* key, std::int64_t value);
    VariantMap& add(const char* key, std::int32_t value);
    VariantMap& add(const char* key, const std::vector<std::string>& values);
    // The floating a{sv}; the map is empty afterwards.
    GVariant* end();

  private:
    GVariantBuilder m_builder;
  };

  // A floating `as`.
  GVariant* stringArray(const std::vector<std::string>& values);
} // namespace tandem
