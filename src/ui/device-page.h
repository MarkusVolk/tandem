/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <adwaita.h>

#include "../device.h"

G_BEGIN_DECLS

#define TANDEM_TYPE_DEVICE_PAGE (tandem_device_page_get_type ())
G_DECLARE_FINAL_TYPE (TandemDevicePage, tandem_device_page, TANDEM, DEVICE_PAGE, AdwNavigationPage)

GtkWidget    *tandem_device_page_new        (TandemDevice *device);
TandemDevice *tandem_device_page_get_device (TandemDevicePage *self);

G_END_DECLS
