/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cli.h"

#include <glib.h>

int
main (int argc, char **argv)
{
  g_set_prgname ("tandem");
  return tandem_cli_run (argc, argv);
}
