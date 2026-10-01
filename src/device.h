/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

#include "config.h"
#include "link.h"
#include "packet.h"

G_BEGIN_DECLS

typedef enum
{
  TANDEM_PAIR_NONE,
  TANDEM_PAIR_REQUESTED,
  TANDEM_PAIR_REQUESTED_BY_PEER,
  TANDEM_PAIR_PAIRED,
} TandemPairState;

#define TANDEM_TYPE_DEVICE (tandem_device_get_type ())
G_DECLARE_FINAL_TYPE (TandemDevice, tandem_device, TANDEM, DEVICE, GObject)

TandemDevice    *tandem_device_new_trusted       (TandemConfig *config, const char *device_id);
TandemDevice    *tandem_device_new_from_link     (TandemConfig *config, TandemLink *link);

const char      *tandem_device_get_id            (TandemDevice *self);
const char      *tandem_device_get_name          (TandemDevice *self);
const char      *tandem_device_get_type_name     (TandemDevice *self);
gint64           tandem_device_get_protocol_version (TandemDevice *self);
gboolean         tandem_device_is_reachable      (TandemDevice *self);
gboolean         tandem_device_is_paired         (TandemDevice *self);
TandemPairState  tandem_device_get_pair_state    (TandemDevice *self);
const char      *tandem_device_get_state_name    (TandemDevice *self);
GTlsCertificate *tandem_device_get_certificate   (TandemDevice *self);
TandemLink      *tandem_device_get_link          (TandemDevice *self);
GInetAddress    *tandem_device_get_last_address  (TandemDevice *self);
const char *const *tandem_device_get_incoming    (TandemDevice *self);
const char *const *tandem_device_get_outgoing    (TandemDevice *self);
gboolean         tandem_device_has_incoming      (TandemDevice *self, const char *type);
gboolean         tandem_device_has_outgoing      (TandemDevice *self, const char *type);

void             tandem_device_set_link          (TandemDevice *self, TandemLink *link);
void             tandem_device_send_packet       (TandemDevice *self, TandemPacket *packet);

G_END_DECLS
