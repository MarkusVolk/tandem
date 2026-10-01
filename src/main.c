/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "app.h"
#include "cli.h"
#include "smoke.h"

#include <string.h>

int
main (int argc, char **argv)
{
  g_autoptr(TandemApp) app = NULL;

  g_set_prgname ("tandem");

  if (argc > 1 && g_str_equal (argv[1], "--smoke-test"))
    return tandem_smoke_run ();
  if (argc > 1 && argv[1][0] != '-')
    return tandem_cli_run (argc, argv);

  app = tandem_app_new ();
  return g_application_run (G_APPLICATION (app), argc, argv);
}
