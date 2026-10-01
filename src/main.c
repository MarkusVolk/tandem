/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "app.h"
#include "smoke.h"

#include <string.h>

int
main (int argc, char **argv)
{
  g_autoptr(TandemApp) app = NULL;

  g_set_prgname ("tandemd");
  if (argc > 1 && g_str_equal (argv[1], "--smoke-test"))
    return tandem_smoke_run ();

  app = tandem_app_new ();
  return g_application_run (G_APPLICATION (app), 1, argv);
}
