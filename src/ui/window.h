/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

typedef struct _TandemApp TandemApp;

#define TANDEM_TYPE_WINDOW (tandem_window_get_type ())
G_DECLARE_FINAL_TYPE (TandemWindow, tandem_window, TANDEM, WINDOW, AdwApplicationWindow)

GtkWidget *tandem_window_new (TandemApp *app);

G_END_DECLS
