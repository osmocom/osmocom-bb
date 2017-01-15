#pragma once

/* Add the 'ctrl' section to VTY, containing the 'bind' command. */
int ctrl_vty_init(void *ctx);

/* Obtain the IP address configured by the 'ctrl'/'bind A.B.C.D' VTY command.
 * This should be fed to ctrl_interface_setup() once the configuration has been
 * read. */
const char *ctrl_vty_get_bind_addr(void);
