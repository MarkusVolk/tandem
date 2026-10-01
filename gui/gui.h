/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <adwaita.h>

#include "daemon.h"

G_BEGIN_DECLS

#define TANDEM_TYPE_GUI_WINDOW (tandem_gui_window_get_type ())
G_DECLARE_FINAL_TYPE (TandemGuiWindow, tandem_gui_window, TANDEM, GUI_WINDOW, AdwApplicationWindow)

GtkWidget *tandem_gui_window_new         (GtkApplication *app);
void       tandem_gui_window_show_device (TandemGuiWindow *self, const char *id);

#define TANDEM_TYPE_GUI_DEVICE_PAGE (tandem_gui_device_page_get_type ())
G_DECLARE_FINAL_TYPE (TandemGuiDevicePage, tandem_gui_device_page, TANDEM, GUI_DEVICE_PAGE, AdwNavigationPage)

GtkWidget *tandem_gui_device_page_new (TandemGuiDevice *device);

GtkWidget *tandem_gui_plugin_group_new (TandemGuiDevice *device, const char *plugin);
const char *tandem_gui_plugin_title    (const char *plugin);
const char *const *tandem_gui_plugin_names (void);

GtkWidget *tandem_gui_sms_page_new (TandemGuiDevice *device);

GtkWidget *tandem_gui_preferences_dialog_new (void);
void       tandem_gui_show_about_dialog      (GtkWidget *parent);

void       tandem_gui_presenter_show (GtkApplication *app, TandemGuiDevice *device);
void       tandem_gui_share_files    (TandemGuiDevice *device, GtkWindow *parent);
void       tandem_gui_reply_dialog   (TandemGuiDevice *device, const char *id, const char *title, GtkWidget *parent);

G_END_DECLS
