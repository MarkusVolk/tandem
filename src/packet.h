/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define TANDEM_PROTOCOL_VERSION 8
#define TANDEM_PROTOCOL_VERSION_MIN 7

#define TANDEM_TYPE_PACKET (tandem_packet_get_type ())

typedef struct _TandemPacket TandemPacket;

struct _TandemPacket
{
  gint64        id;
  char         *type;
  JsonObject   *body;
  gint64        payload_size;
  JsonObject   *transfer_info;
  GInputStream *payload_stream;
  int           ref_count;
};

GType         tandem_packet_get_type       (void);
TandemPacket *tandem_packet_new            (const char *type);
TandemPacket *tandem_packet_ref            (TandemPacket *packet);
void          tandem_packet_unref          (TandemPacket *packet);
TandemPacket *tandem_packet_from_line      (const char *line, gssize len, GError **error);
char         *tandem_packet_to_line        (TandemPacket *packet, gsize *len);
gboolean      tandem_packet_is_type        (TandemPacket *packet, const char *type);
gboolean      tandem_packet_has_payload    (TandemPacket *packet);
int           tandem_packet_get_payload_port (TandemPacket *packet);
void          tandem_packet_set_payload    (TandemPacket *packet, GInputStream *stream, gint64 size);

const char   *tandem_packet_get_string     (TandemPacket *packet, const char *member, const char *fallback);
gint64        tandem_packet_get_int        (TandemPacket *packet, const char *member, gint64 fallback);
gboolean      tandem_packet_get_boolean    (TandemPacket *packet, const char *member, gboolean fallback);
double        tandem_packet_get_double     (TandemPacket *packet, const char *member, double fallback);
gboolean      tandem_packet_has            (TandemPacket *packet, const char *member);
JsonArray    *tandem_packet_get_array      (TandemPacket *packet, const char *member);
JsonObject   *tandem_packet_get_object     (TandemPacket *packet, const char *member);

void          tandem_packet_set_string     (TandemPacket *packet, const char *member, const char *value);
void          tandem_packet_set_int        (TandemPacket *packet, const char *member, gint64 value);
void          tandem_packet_set_boolean    (TandemPacket *packet, const char *member, gboolean value);
void          tandem_packet_set_double     (TandemPacket *packet, const char *member, double value);
void          tandem_packet_set_strv       (TandemPacket *packet, const char *member, const char *const *values);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (TandemPacket, tandem_packet_unref)

G_END_DECLS
