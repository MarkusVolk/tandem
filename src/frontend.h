/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

void tandem_frontend_activate (void);
void tandem_frontend_action   (const char *action, GVariant *parameter);

G_END_DECLS
