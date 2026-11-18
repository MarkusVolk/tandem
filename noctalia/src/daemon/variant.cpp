#include "daemon/variant.h"

#include <utility>

namespace {
  GVariant* unbox(GVariant* value) {
    while (value != nullptr && g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT)) {
      GVariant* inner = g_variant_get_variant(value);
      g_variant_unref(value);
      value = inner;
    }
    return value;
  }
} // namespace

tandem::Variant::Variant(GVariant* value) : m_value(value ? g_variant_ref_sink(value) : nullptr) {}

tandem::Variant tandem::Variant::adopt(GVariant* value) {
  Variant result;
  result.m_value = value;
  return result;
}

tandem::Variant::Variant(const Variant& other) : m_value(other.m_value ? g_variant_ref(other.m_value) : nullptr) {}

tandem::Variant::Variant(Variant&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}

tandem::Variant& tandem::Variant::operator=(Variant other) noexcept {
  std::swap(m_value, other.m_value);
  return *this;
}

tandem::Variant::~Variant() {
  if (m_value != nullptr) {
    g_variant_unref(m_value);
  }
}

bool tandem::Variant::is(const GVariantType* type) const {
  return m_value != nullptr && g_variant_is_of_type(m_value, type);
}

std::string tandem::Variant::string() const {
  if (is(G_VARIANT_TYPE_STRING) || is(G_VARIANT_TYPE_OBJECT_PATH)) {
    return g_variant_get_string(m_value, nullptr);
  }
  if (is(G_VARIANT_TYPE_BYTESTRING)) {
    return g_variant_get_bytestring(m_value);
  }
  return {};
}

std::int64_t tandem::Variant::integer() const {
  if (m_value == nullptr) {
    return 0;
  }
  switch (g_variant_classify(m_value)) {
  case G_VARIANT_CLASS_BYTE:
    return g_variant_get_byte(m_value);
  case G_VARIANT_CLASS_INT16:
    return g_variant_get_int16(m_value);
  case G_VARIANT_CLASS_UINT16:
    return g_variant_get_uint16(m_value);
  case G_VARIANT_CLASS_INT32:
    return g_variant_get_int32(m_value);
  case G_VARIANT_CLASS_UINT32:
    return g_variant_get_uint32(m_value);
  case G_VARIANT_CLASS_INT64:
    return g_variant_get_int64(m_value);
  case G_VARIANT_CLASS_UINT64:
    return static_cast<std::int64_t>(g_variant_get_uint64(m_value));
  case G_VARIANT_CLASS_DOUBLE:
    return static_cast<std::int64_t>(g_variant_get_double(m_value));
  default:
    return 0;
  }
}

bool tandem::Variant::boolean() const { return is(G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(m_value); }

double tandem::Variant::number() const {
  if (is(G_VARIANT_TYPE_DOUBLE)) {
    return g_variant_get_double(m_value);
  }
  return static_cast<double>(integer());
}

std::vector<std::string> tandem::Variant::strings() const {
  std::vector<std::string> result;
  if (!is(G_VARIANT_TYPE_STRING_ARRAY)) {
    return result;
  }
  gsize count = 0;
  const gchar** values = g_variant_get_strv(m_value, &count);
  result.reserve(count);
  for (gsize i = 0; i < count; ++i) {
    result.emplace_back(values[i]);
  }
  g_free(static_cast<gpointer>(values));
  return result;
}

std::vector<tandem::Variant> tandem::Variant::children() const {
  std::vector<Variant> result;
  if (m_value == nullptr || !g_variant_is_container(m_value)) {
    return result;
  }
  const gsize count = g_variant_n_children(m_value);
  result.reserve(count);
  for (gsize i = 0; i < count; ++i) {
    result.push_back(adopt(unbox(g_variant_get_child_value(m_value, i))));
  }
  return result;
}

tandem::Variant tandem::Variant::lookup(std::string_view key) const {
  if (!is(G_VARIANT_TYPE_VARDICT)) {
    return {};
  }
  const std::string name(key);
  return adopt(unbox(g_variant_lookup_value(m_value, name.c_str(), nullptr)));
}

bool tandem::Variant::has(std::string_view key) const { return static_cast<bool>(lookup(key)); }

std::string tandem::Variant::string(std::string_view key, std::string_view fallback) const {
  const Variant value = lookup(key);
  return value.is(G_VARIANT_TYPE_STRING) ? value.string() : std::string(fallback);
}

std::int64_t tandem::Variant::integer(std::string_view key, std::int64_t fallback) const {
  const Variant value = lookup(key);
  if (!value || value.is(G_VARIANT_TYPE_STRING) || value.is(G_VARIANT_TYPE_BOOLEAN) || g_variant_is_container(value.get())) {
    return fallback;
  }
  return value.integer();
}

bool tandem::Variant::boolean(std::string_view key, bool fallback) const {
  const Variant value = lookup(key);
  return value.is(G_VARIANT_TYPE_BOOLEAN) ? value.boolean() : fallback;
}

double tandem::Variant::number(std::string_view key, double fallback) const {
  const Variant value = lookup(key);
  if (value.is(G_VARIANT_TYPE_DOUBLE)) {
    return value.number();
  }
  if (!value || value.is(G_VARIANT_TYPE_STRING) || value.is(G_VARIANT_TYPE_BOOLEAN) || g_variant_is_container(value.get())) {
    return fallback;
  }
  return value.number();
}

std::vector<std::string> tandem::Variant::strings(std::string_view key) const { return lookup(key).strings(); }

std::vector<tandem::Variant> tandem::Variant::maps(std::string_view key) const {
  const Variant value = lookup(key);
  std::vector<Variant> result;
  if (!value.is(G_VARIANT_TYPE("aa{sv}"))) {
    return result;
  }
  return value.children();
}

std::vector<std::uint8_t> tandem::Variant::bytes(std::string_view key) const {
  const Variant value = lookup(key);
  if (!value.is(G_VARIANT_TYPE_BYTESTRING)) {
    return {};
  }
  gsize size = 0;
  const auto* data = static_cast<const std::uint8_t*>(g_variant_get_fixed_array(value.get(), &size, 1));
  return {data, data + size};
}

tandem::VariantMap::VariantMap() { g_variant_builder_init(&m_builder, G_VARIANT_TYPE_VARDICT); }

tandem::VariantMap::~VariantMap() { g_variant_builder_clear(&m_builder); }

tandem::VariantMap& tandem::VariantMap::add(const char* key, GVariant* value) {
  g_variant_builder_add(&m_builder, "{sv}", key, value);
  return *this;
}

tandem::VariantMap& tandem::VariantMap::add(const char* key, const std::string& value) {
  return add(key, g_variant_new_string(value.c_str()));
}

tandem::VariantMap& tandem::VariantMap::add(const char* key, const char* value) {
  return add(key, g_variant_new_string(value));
}

tandem::VariantMap& tandem::VariantMap::add(const char* key, bool value) {
  return add(key, g_variant_new_boolean(value));
}

tandem::VariantMap& tandem::VariantMap::add(const char* key, std::int64_t value) {
  return add(key, g_variant_new_int64(value));
}

tandem::VariantMap& tandem::VariantMap::add(const char* key, std::int32_t value) {
  return add(key, g_variant_new_int32(value));
}

tandem::VariantMap& tandem::VariantMap::add(const char* key, const std::vector<std::string>& values) {
  return add(key, stringArray(values));
}

GVariant* tandem::VariantMap::end() { return g_variant_builder_end(&m_builder); }

GVariant* tandem::stringArray(const std::vector<std::string>& values) {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE_STRING_ARRAY);
  for (const auto& value : values) {
    g_variant_builder_add(&builder, "s", value.c_str());
  }
  return g_variant_builder_end(&builder);
}
