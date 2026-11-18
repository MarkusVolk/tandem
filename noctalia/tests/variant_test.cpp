#include "check.h"
#include "daemon/client.h"
#include "daemon/variant.h"

using tandem::Variant;
using tandem::VariantMap;

namespace {
  Variant parse(const char* text) {
    GError* error = nullptr;
    GVariant* value = g_variant_parse(nullptr, text, nullptr, nullptr, &error);
    CHECK(value != nullptr);
    return Variant::adopt(value);
  }

  void readsPluginState() {
    const Variant state = parse("{'charge': <int32 42>, 'charging': <true>, 'summary': <'LTE'>, 'x': <0.25>}");
    CHECK(state.integer("charge", -1) == 42);
    CHECK(state.boolean("charging"));
    CHECK(state.string("summary") == "LTE");
    CHECK(state.number("x") == 0.25);
    CHECK(state.integer("missing", -1) == -1);
    CHECK(state.string("charge", "fallback") == "fallback");
    CHECK(!state.boolean("summary"));
  }

  void readsNestedMaps() {
    const Variant state = parse(
        "{'threads': <[{'id': <int64 7>, 'title': <'Anna'>, 'addresses': <['+49 1']>,"
        " 'last': <{'body': <'hi'>, 'date': <int64 1700000000000>, 'read': <false>}>}]>}"
    );
    const auto threads = state.maps("threads");
    CHECK(threads.size() == 1);
    CHECK(threads[0].integer("id") == 7);
    CHECK(threads[0].strings("addresses").size() == 1);
    const Variant last = threads[0].lookup("last");
    CHECK(last.string("body") == "hi");
    CHECK(last.integer("date") == 1700000000000);
    CHECK(!last.boolean("read", true));
    CHECK(state.maps("missing").empty());
  }

  void readsTuplesAndBytes() {
    const Variant commands = parse("[('k1', 'Lock', 'loginctl lock-session')]");
    const auto entries = commands.children();
    CHECK(entries.size() == 1);
    const auto fields = entries[0].children();
    CHECK(fields.size() == 3 && fields[2].string() == "loginctl lock-session");
    const Variant attachment = parse("{'thumbnail': <[byte 0x89, 0x50, 0x4e, 0x47]>}");
    CHECK(attachment.bytes("thumbnail").size() == 4);
  }

  void buildsArguments() {
    const Variant args(VariantMap().add("thread", std::int64_t{5}).add("count", std::int32_t{50}).add("body", "x").end());
    CHECK(args.is(G_VARIANT_TYPE_VARDICT));
    CHECK(args.lookup("thread").is(G_VARIANT_TYPE_INT64));
    CHECK(args.lookup("count").is(G_VARIANT_TYPE_INT32));
    CHECK(args.string("body") == "x");
  }

  void pathsLikeTheDaemon() {
    CHECK(tandem::devicePath("a1-b2_c3") == "/de/flk/Tandem/devices/d_a1_b2_c3");
  }
} // namespace

int main() {
  readsPluginState();
  readsNestedMaps();
  readsTuplesAndBytes();
  buildsArguments();
  pathsLikeTheDaemon();
  return 0;
}
