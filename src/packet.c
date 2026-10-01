/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "packet.h"
#include "util.h"

#include <string.h>

G_DEFINE_BOXED_TYPE (TandemPacket, tandem_packet, tandem_packet_ref, tandem_packet_unref)

TandemPacket *
tandem_packet_new (const char *type)
{
  TandemPacket *packet = g_new0 (TandemPacket, 1);

  packet->ref_count = 1;
  packet->id = tandem_now_ms ();
  packet->type = g_strdup (type);
  packet->body = json_object_new ();
  packet->payload_size = -1;
  return packet;
}

TandemPacket *
tandem_packet_ref (TandemPacket *packet)
{
  g_atomic_int_inc (&packet->ref_count);
  return packet;
}

void
tandem_packet_unref (TandemPacket *packet)
{
  if (packet == NULL || !g_atomic_int_dec_and_test (&packet->ref_count))
    return;
  g_free (packet->type);
  g_clear_pointer (&packet->body, json_object_unref);
  g_clear_pointer (&packet->transfer_info, json_object_unref);
  g_clear_object (&packet->payload_stream);
  g_free (packet);
}

TandemPacket *
tandem_packet_from_line (const char *line, gssize len, GError **error)
{
  g_autoptr(JsonParser) parser = json_parser_new ();
  JsonNode *root;
  JsonObject *object;
  TandemPacket *packet;

  if (!json_parser_load_from_data (parser, line, len, error))
    return NULL;
  root = json_parser_get_root (parser);
  if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Packet is not a JSON object");
      return NULL;
    }
  object = json_node_get_object (root);
  if (!json_object_has_member (object, "type") || !json_object_has_member (object, "body") ||
      !JSON_NODE_HOLDS_OBJECT (json_object_get_member (object, "body")))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Packet without type or body");
      return NULL;
    }

  packet = g_new0 (TandemPacket, 1);
  packet->ref_count = 1;
  packet->id = json_object_has_member (object, "id") ? json_object_get_int_member (object, "id") : 0;
  packet->type = g_strdup (json_object_get_string_member (object, "type"));
  packet->body = json_object_ref (json_object_get_object_member (object, "body"));
  packet->payload_size = -1;
  if (json_object_has_member (object, "payloadSize"))
    packet->payload_size = json_object_get_int_member (object, "payloadSize");
  if (json_object_has_member (object, "payloadTransferInfo") &&
      JSON_NODE_HOLDS_OBJECT (json_object_get_member (object, "payloadTransferInfo")))
    packet->transfer_info = json_object_ref (json_object_get_object_member (object, "payloadTransferInfo"));
  return packet;
}

char *
tandem_packet_to_line (TandemPacket *packet, gsize *len)
{
  g_autoptr(JsonBuilder) builder = json_builder_new ();
  g_autoptr(JsonGenerator) generator = json_generator_new ();
  g_autoptr(JsonNode) root = NULL;
  g_autofree char *json = NULL;
  gsize json_len;

  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "id");
  json_builder_add_int_value (builder, packet->id);
  json_builder_set_member_name (builder, "type");
  json_builder_add_string_value (builder, packet->type);
  json_builder_set_member_name (builder, "body");
  json_builder_add_value (builder, json_node_init_object (json_node_alloc (), packet->body));
  if (packet->payload_size >= 0)
    {
      json_builder_set_member_name (builder, "payloadSize");
      json_builder_add_int_value (builder, packet->payload_size);
    }
  if (packet->transfer_info != NULL)
    {
      json_builder_set_member_name (builder, "payloadTransferInfo");
      json_builder_add_value (builder, json_node_init_object (json_node_alloc (), packet->transfer_info));
    }
  json_builder_end_object (builder);

  root = json_builder_get_root (builder);
  json_generator_set_root (generator, root);
  json_generator_set_pretty (generator, FALSE);
  json = json_generator_to_data (generator, &json_len);
  if (len != NULL)
    *len = json_len + 1;
  return g_strconcat (json, "\n", NULL);
}

gboolean
tandem_packet_is_type (TandemPacket *packet, const char *type)
{
  return g_strcmp0 (packet->type, type) == 0;
}

gboolean
tandem_packet_has_payload (TandemPacket *packet)
{
  return packet->payload_size >= 0 && packet->transfer_info != NULL &&
         json_object_has_member (packet->transfer_info, "port");
}

int
tandem_packet_get_payload_port (TandemPacket *packet)
{
  if (!tandem_packet_has_payload (packet))
    return -1;
  return (int) json_object_get_int_member (packet->transfer_info, "port");
}

void
tandem_packet_set_payload (TandemPacket *packet, GInputStream *stream, gint64 size)
{
  g_set_object (&packet->payload_stream, stream);
  packet->payload_size = size;
}

const char *
tandem_packet_get_string (TandemPacket *packet, const char *member, const char *fallback)
{
  JsonNode *node = json_object_get_member (packet->body, member);

  if (node == NULL || !JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_STRING)
    return fallback;
  return json_node_get_string (node);
}

gint64
tandem_packet_get_int (TandemPacket *packet, const char *member, gint64 fallback)
{
  JsonNode *node = json_object_get_member (packet->body, member);

  if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
    return fallback;
  if (json_node_get_value_type (node) == G_TYPE_DOUBLE)
    return (gint64) json_node_get_double (node);
  if (json_node_get_value_type (node) == G_TYPE_INT64)
    return json_node_get_int (node);
  if (json_node_get_value_type (node) == G_TYPE_BOOLEAN)
    return json_node_get_boolean (node);
  return fallback;
}

gboolean
tandem_packet_get_boolean (TandemPacket *packet, const char *member, gboolean fallback)
{
  JsonNode *node = json_object_get_member (packet->body, member);

  if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
    return fallback;
  if (json_node_get_value_type (node) == G_TYPE_BOOLEAN)
    return json_node_get_boolean (node);
  if (json_node_get_value_type (node) == G_TYPE_INT64)
    return json_node_get_int (node) != 0;
  return fallback;
}

double
tandem_packet_get_double (TandemPacket *packet, const char *member, double fallback)
{
  JsonNode *node = json_object_get_member (packet->body, member);

  if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
    return fallback;
  if (json_node_get_value_type (node) == G_TYPE_DOUBLE)
    return json_node_get_double (node);
  if (json_node_get_value_type (node) == G_TYPE_INT64)
    return (double) json_node_get_int (node);
  return fallback;
}

gboolean
tandem_packet_has (TandemPacket *packet, const char *member)
{
  return json_object_has_member (packet->body, member);
}

JsonArray *
tandem_packet_get_array (TandemPacket *packet, const char *member)
{
  JsonNode *node = json_object_get_member (packet->body, member);

  if (node == NULL || !JSON_NODE_HOLDS_ARRAY (node))
    return NULL;
  return json_node_get_array (node);
}

JsonObject *
tandem_packet_get_object (TandemPacket *packet, const char *member)
{
  JsonNode *node = json_object_get_member (packet->body, member);

  if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
    return NULL;
  return json_node_get_object (node);
}

void
tandem_packet_set_string (TandemPacket *packet, const char *member, const char *value)
{
  json_object_set_string_member (packet->body, member, value);
}

void
tandem_packet_set_int (TandemPacket *packet, const char *member, gint64 value)
{
  json_object_set_int_member (packet->body, member, value);
}

void
tandem_packet_set_boolean (TandemPacket *packet, const char *member, gboolean value)
{
  json_object_set_boolean_member (packet->body, member, value);
}

void
tandem_packet_set_double (TandemPacket *packet, const char *member, double value)
{
  json_object_set_double_member (packet->body, member, value);
}

void
tandem_packet_set_strv (TandemPacket *packet, const char *member, const char *const *values)
{
  JsonArray *array = json_array_new ();

  for (; values != NULL && *values != NULL; values++)
    json_array_add_string_element (array, *values);
  json_object_set_array_member (packet->body, member, array);
}
