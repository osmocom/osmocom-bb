
/*! \mainpage libosmovty Documentation
 *
 * \section sec_intro Introduction
 * This library is a collection of common code used in various
 * GSM related sub-projects inside the Osmocom family of projects.  It
 * has been imported/derived from the GNU Zebra project.
 * \n\n
 * libosmovty implements the interactive command-line on the VTY
 * (Virtual TTY) as well as configuration file parsing.
 * \n\n
 * Please note that C language projects inside Osmocom are typically
 * single-threaded event-loop state machine designs.  As such,
 * routines in libosmovty are not thread-safe.  If you must use them in
 * a multi-threaded context, you have to add your own locking.
 *
 * \section sec_copyright Copyright and License
 * Copyright © 1997-2007 - Kuninhiro Ishiguro\n
 * Copyright © 2008-2011 - Harald Welte, Holger Freyther and contributors\n
 * All rights reserved. \n\n
 * The source code of libosmovty is licensed under the terms of the GNU
 * General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later
 * version.\n
 * See <http://www.gnu.org/licenses/> or COPYING included in the source
 * code package istelf.\n
 * The information detailed here is provided AS IS with NO WARRANTY OF
 * ANY KIND, INCLUDING THE WARRANTY OF DESIGN, MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.
 * \n\n
 *
 * \section sec_contact Contact and Support
 * Community-based support is available at the OpenBSC mailing list
 * <http://lists.osmocom.org/mailman/listinfo/openbsc>\n
 * Commercial support options available upon request from
 * <http://sysmocom.de/>
 */


#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <termios.h>

#include <sys/utsname.h>
#include <sys/param.h>

#include <arpa/telnet.h>

#include <osmocom/vty/vty.h>
#include <osmocom/vty/command.h>
#include <osmocom/vty/buffer.h>
#include <osmocom/core/talloc.h>

/* \addtogroup vty
 * @{
 */
/*! \file vty.c */

#define SYSCONFDIR "/usr/local/etc"

/* our callback, located in telnet_interface.c */
void vty_event(enum event event, int sock, struct vty *vty);

extern struct host host;

/* Vector which store each vty structure. */
static vector vtyvec;

vector Vvty_serv_thread;

char *vty_cwd = NULL;

/* IP address passed to the 'line vty'/'bind' command.
 * Setting the default as vty_bind_addr = "127.0.0.1" doesn't allow freeing, so
 * use NULL and VTY_BIND_ADDR_DEFAULT instead. */
static const char *vty_bind_addr = NULL;
#define VTY_BIND_ADDR_DEFAULT "127.0.0.1"

/* Configure lock. */
static int vty_config;

static int password_check;

void *tall_vty_ctx;

static void vty_clear_buf(struct vty *vty)
{
	memset(vty->buf, 0, vty->max);
}

/*! \brief Allocate a new vty interface structure */
struct vty *vty_new(void)
{
	struct vty *new = talloc_zero(tall_vty_ctx, struct vty);

	if (!new)
		goto out;

	new->obuf = buffer_new(new, 0);	/* Use default buffer size. */
	if (!new->obuf)
		goto out_new;
	new->buf = _talloc_zero(new, VTY_BUFSIZ, "vty_new->buf");
	if (!new->buf)
		goto out_obuf;

	new->max = VTY_BUFSIZ;

	return new;

out_obuf:
	buffer_free(new->obuf);
out_new:
	talloc_free(new);
	new = NULL;
out:
	return new;
}

/* Authentication of vty */
static void vty_auth(struct vty *vty, char *buf)
{
	char *passwd = NULL;
	enum node_type next_node = 0;
	int fail;
	char *crypt(const char *, const char *);

	switch (vty->node) {
	case AUTH_NODE:
#ifdef VTY_CRYPT_PW
		if (host.encrypt)
			passwd = host.password_encrypt;
		else
#endif
			passwd = host.password;
		if (host.advanced)
			next_node = host.enable ? VIEW_NODE : ENABLE_NODE;
		else
			next_node = VIEW_NODE;
		break;
	case AUTH_ENABLE_NODE:
#ifdef VTY_CRYPT_PW
		if (host.encrypt)
			passwd = host.enable_encrypt;
		else
#endif
			passwd = host.enable;
		next_node = ENABLE_NODE;
		break;
	}

	if (passwd) {
#ifdef VTY_CRYPT_PW
		if (host.encrypt)
			fail = strcmp(crypt(buf, passwd), passwd);
		else
#endif
			fail = strcmp(buf, passwd);
	} else
		fail = 1;

	if (!fail) {
		vty->fail = 0;
		vty->node = next_node;	/* Success ! */
	} else {
		vty->fail++;
		if (vty->fail >= 3) {
			if (vty->node == AUTH_NODE) {
				vty_out(vty,
					"%% Bad passwords, too many failures!%s",
					VTY_NEWLINE);
				vty->status = VTY_CLOSE;
			} else {
				/* AUTH_ENABLE_NODE */
				vty->fail = 0;
				vty_out(vty,
					"%% Bad enable passwords, too many failures!%s",
					VTY_NEWLINE);
				vty->node = VIEW_NODE;
			}
		}
	}
}

/*! \brief Close a given vty interface. */
void vty_close(struct vty *vty)
{
	int i;

	if (vty->obuf)  {
		/* Flush buffer. */
		buffer_flush_all(vty->obuf, vty->fd);

		/* Free input buffer. */
		buffer_free(vty->obuf);
		vty->obuf = NULL;
	}

	/* Free command history. */
	for (i = 0; i < VTY_MAXHIST; i++)
		if (vty->hist[i])
			talloc_free(vty->hist[i]);

	/* Unset vector. */
	vector_unset(vtyvec, vty->fd);

	/* Close socket. */
	if (vty->fd > 0)
		close(vty->fd);

	if (vty->buf) {
		talloc_free(vty->buf);
		vty->buf = NULL;
	}

	/* Check configure. */
	vty_config_unlock(vty);

	/* VTY_CLOSED is handled by the telnet_interface */
	vty_event(VTY_CLOSED, vty->fd, vty);

	/* OK free vty. */
	talloc_free(vty);
}

/*! \brief Return if this VTY is a shell or not */
int vty_shell(struct vty *vty)
{
	return vty->type == VTY_SHELL ? 1 : 0;
}


/*! \brief VTY standard output function
 *  \param[in] vty VTY to which we should print
 *  \param[in] format variable-length format string
 */
int vty_out(struct vty *vty, const char *format, ...)
{
	va_list args;
	int len = 0;
	int size = 1024;
	char buf[1024];
	char *p = NULL;

	if (vty_shell(vty)) {
		va_start(args, format);
		vprintf(format, args);
		va_end(args);
	} else {
		/* Try to write to initial buffer.  */
		va_start(args, format);
		len = vsnprintf(buf, sizeof buf, format, args);
		va_end(args);

		/* Initial buffer is not enough.  */
		if (len < 0 || len >= size) {
			while (1) {
				if (len > -1)
					size = len + 1;
				else
					size = size * 2;

				p = talloc_realloc_size(vty, p, size);
				if (!p)
					return -1;

				va_start(args, format);
				len = vsnprintf(p, size, format, args);
				va_end(args);

				if (len > -1 && len < size)
					break;
			}
		}

		/* When initial buffer is enough to store all output.  */
		if (!p)
			p = buf;

		/* Pointer p must point out buffer. */
		buffer_put(vty->obuf, (unsigned char *) p, len);

		/* If p is not different with buf, it is allocated buffer.  */
		if (p != buf)
			talloc_free(p);
	}

	vty_event(VTY_WRITE, vty->fd, vty);

	return len;
}

/*! \brief print a newline on the given VTY */
int vty_out_newline(struct vty *vty)
{
	const char *p = vty_newline(vty);
	buffer_put(vty->obuf, p, strlen(p));
	return 0;
}

/*! \brief return the current index of a given VTY */
void *vty_current_index(struct vty *vty)
{
	return vty->index;
}

/*! \brief return the current node of a given VTY */
int vty_current_node(struct vty *vty)
{
	return vty->node;
}

/*! \brief Lock the configuration to a given VTY
 *  \param[in] vty VTY to which the config shall be locked
 *  \returns 1 on success, 0 on error
 *
 * This shall be used to make sure only one VTY at a given time has
 * access to modify the configuration */
int vty_config_lock(struct vty *vty)
{
	if (vty_config == 0) {
		vty->config = 1;
		vty_config = 1;
	}
	return vty->config;
}

/*! \brief Unlock the configuration from a given VTY
 *  \param[in] vty VTY from which the configuration shall be unlocked
 *  \returns 0 in case of success
 */
int vty_config_unlock(struct vty *vty)
{
	if (vty_config == 1 && vty->config == 1) {
		vty->config = 0;
		vty_config = 0;
	}
	return vty->config;
}

/* Say hello to vty interface. */
void vty_hello(struct vty *vty)
{
	const char *app_name = "<unnamed>";

	if (host.app_info->name)
		app_name = host.app_info->name;

	vty_out(vty, "Welcome to the %s control interface%s%s",
		app_name, VTY_NEWLINE, VTY_NEWLINE);

	if (host.app_info->copyright)
		vty_out(vty, "%s", host.app_info->copyright);

	if (host.motdfile) {
		FILE *f;
		char buf[4096];

		f = fopen(host.motdfile, "r");
		if (f) {
			while (fgets(buf, sizeof(buf), f)) {
				char *s;
				/* work backwards to ignore trailling isspace() */
				for (s = buf + strlen(buf);
				     (s > buf) && isspace(*(s - 1)); s--) ;
				*s = '\0';
				vty_out(vty, "%s%s", buf, VTY_NEWLINE);
			}
			fclose(f);
		} else
			vty_out(vty, "MOTD file not found%s", VTY_NEWLINE);
	} else if (host.motd)
		vty_out(vty, "%s", host.motd);
}

/* Put out prompt and wait input from user. */
static void vty_prompt(struct vty *vty)
{
	struct utsname names;
	const char *hostname;

	if (vty->type == VTY_TERM) {
		hostname = host.app_info->name;
		if (!hostname) {
			uname(&names);
			hostname = names.nodename;
		}
		vty_out(vty, cmd_prompt(vty->node), hostname);
	}
}

/* Command execution over the vty interface. */
static int vty_command(struct vty *vty, char *buf)
{
	int ret;
	vector vline;

	/* Split readline string up into the vector */
	vline = cmd_make_strvec(buf);

	if (vline == NULL)
		return CMD_SUCCESS;

	ret = cmd_execute_command(vline, vty, NULL, 0);
	if (ret != CMD_SUCCESS)
		switch (ret) {
		case CMD_WARNING:
			if (vty->type == VTY_FILE)
				vty_out(vty, "Warning...%s", VTY_NEWLINE);
			break;
		case CMD_ERR_AMBIGUOUS:
			vty_out(vty, "%% Ambiguous command.%s", VTY_NEWLINE);
			break;
		case CMD_ERR_NO_MATCH:
			vty_out(vty, "%% Unknown command.%s", VTY_NEWLINE);
			break;
		case CMD_ERR_INCOMPLETE:
			vty_out(vty, "%% Command incomplete.%s", VTY_NEWLINE);
			break;
		}
	cmd_free_strvec(vline);

	return ret;
}

static const char telnet_backward_char = 0x08;
static const char telnet_space_char = ' ';

/* Basic function to write buffer to vty. */
static void vty_write(struct vty *vty, const char *buf, size_t nbytes)
{
	if ((vty->node == AUTH_NODE) || (vty->node == AUTH_ENABLE_NODE))
		return;

	/* Should we do buffering here ?  And make vty_flush (vty) ? */
	buffer_put(vty->obuf, buf, nbytes);
}

/* Ensure length of input buffer.  Is buffer is short, double it. */
static void vty_ensure(struct vty *vty, int length)
{
	if (vty->max <= length) {
		vty->max *= 2;
		vty->buf = talloc_realloc_size(vty, vty->buf, vty->max);
		// FIXME: check return
	}
}

/* Basic function to insert character into vty. */
static void vty_self_insert(struct vty *vty, char c)
{
	int i;
	int length;

	vty_ensure(vty, vty->length + 1);
	length = vty->length - vty->cp;
	memmove(&vty->buf[vty->cp + 1], &vty->buf[vty->cp], length);
	vty->buf[vty->cp] = c;

	vty_write(vty, &vty->buf[vty->cp], length + 1);
	for (i = 0; i < length; i++)
		vty_write(vty, &telnet_backward_char, 1);

	vty->cp++;
	vty->length++;
}

/* Self insert character 'c' in overwrite mode. */
static void vty_self_insert_overwrite(struct vty *vty, char c)
{
	vty_ensure(vty, vty->length + 1);
	vty->buf[vty->cp++] = c;

	if (vty->cp > vty->length)
		vty->length++;

	if ((vty->node == AUTH_NODE) || (vty->node == AUTH_ENABLE_NODE))
		return;

	vty_write(vty, &c, 1);
}

/* Insert a word into vty interface with overwrite mode. */
static void vty_insert_word_overwrite(struct vty *vty, char *str)
{
	int len = strlen(str);
	vty_write(vty, str, len);
	strcpy(&vty->buf[vty->cp], str);
	vty->cp += len;
	vty->length = vty->cp;
}

/* Forward character. */
static void vty_forward_char(struct vty *vty)
{
	if (vty->cp < vty->length) {
		vty_write(vty, &vty->buf[vty->cp], 1);
		vty->cp++;
	}
}

/* Backward character. */
static void vty_backward_char(struct vty *vty)
{
	if (vty->cp > 0) {
		vty->cp--;
		vty_write(vty, &telnet_backward_char, 1);
	}
}

/* Move to the beginning of the line. */
static void vty_beginning_of_line(struct vty *vty)
{
	while (vty->cp)
		vty_backward_char(vty);
}

/* Move to the end of the line. */
static void vty_end_of_line(struct vty *vty)
{
	while (vty->cp < vty->length)
		vty_forward_char(vty);
}

/* Add current command line to the history buffer. */
static void vty_hist_add(struct vty *vty)
{
	int index;

	if (vty->length == 0)
		return;

	index = vty->hindex ? vty->hindex - 1 : VTY_MAXHIST - 1;

	/* Ignore the same string as previous one. */
	if (vty->hist[index])
		if (strcmp(vty->buf, vty->hist[index]) == 0) {
			vty->hp = vty->hindex;
			return;
		}

	/* Insert history entry. */
	if (vty->hist[vty->hindex])
		talloc_free(vty->hist[vty->hindex]);
	vty->hist[vty->hindex] = talloc_strdup(vty, vty->buf);

	/* History index rotation. */
	vty->hindex++;
	if (vty->hindex == VTY_MAXHIST)
		vty->hindex = 0;

	vty->hp = vty->hindex;
}

/* Get telnet window size. */
static int
vty_telnet_option (struct vty *vty, unsigned char *buf, int nbytes)
{
#ifdef TELNET_OPTION_DEBUG
  int i;

  for (i = 0; i < nbytes; i++)
    {
      switch (buf[i])
	{
	case IAC:
	  vty_out (vty, "IAC ");
	  break;
	case WILL:
	  vty_out (vty, "WILL ");
	  break;
	case WONT:
	  vty_out (vty, "WONT ");
	  break;
	case DO:
	  vty_out (vty, "DO ");
	  break;
	case DONT:
	  vty_out (vty, "DONT ");
	  break;
	case SB:
	  vty_out (vty, "SB ");
	  break;
	case SE:
	  vty_out (vty, "SE ");
	  break;
	case TELOPT_ECHO:
	  vty_out (vty, "TELOPT_ECHO %s", VTY_NEWLINE);
	  break;
	case TELOPT_SGA:
	  vty_out (vty, "TELOPT_SGA %s", VTY_NEWLINE);
	  break;
	case TELOPT_NAWS:
	  vty_out (vty, "TELOPT_NAWS %s", VTY_NEWLINE);
	  break;
	default:
	  vty_out (vty, "%x ", buf[i]);
	  break;
	}
    }
  vty_out (vty, "%s", VTY_NEWLINE);

#endif /* TELNET_OPTION_DEBUG */

  switch (buf[0])
    {
    case SB:
      vty->sb_len = 0;
      vty->iac_sb_in_progress = 1;
      return 0;
      break;
    case SE:
      {
	if (!vty->iac_sb_in_progress)
	  return 0;

	if ((vty->sb_len == 0) || (vty->sb_buf[0] == '\0'))
	  {
	    vty->iac_sb_in_progress = 0;
	    return 0;
	  }
	switch (vty->sb_buf[0])
	  {
	  case TELOPT_NAWS:
	    if (vty->sb_len != TELNET_NAWS_SB_LEN)
	      vty_out(vty,"RFC 1073 violation detected: telnet NAWS option "
			"should send %d characters, but we received %lu",
			TELNET_NAWS_SB_LEN, (unsigned long)vty->sb_len);
	    else if (sizeof(vty->sb_buf) < TELNET_NAWS_SB_LEN)
	      vty_out(vty, "Bug detected: sizeof(vty->sb_buf) %lu < %d, "
		       "too small to handle the telnet NAWS option",
		       (unsigned long)sizeof(vty->sb_buf), TELNET_NAWS_SB_LEN);
	    else
	      {
		vty->width = ((vty->sb_buf[1] << 8)|vty->sb_buf[2]);
		vty->height = ((vty->sb_buf[3] << 8)|vty->sb_buf[4]);
#ifdef TELNET_OPTION_DEBUG
		vty_out(vty, "TELNET NAWS window size negotiation completed: "
			      "width %d, height %d%s",
			vty->width, vty->height, VTY_NEWLINE);
#endif
	      }
	    break;
	  }
	vty->iac_sb_in_progress = 0;
	return 0;
	break;
      }
    default:
      break;
    }
  return 1;
}

/* Execute current command line. */
static int vty_execute(struct vty *vty)
{
	int ret;

	ret = CMD_SUCCESS;

	switch (vty->node) {
	case AUTH_NODE:
	case AUTH_ENABLE_NODE:
		vty_auth(vty, vty->buf);
		break;
	default:
		ret = vty_command(vty, vty->buf);
		if (vty->type == VTY_TERM)
			vty_hist_add(vty);
		break;
	}

	/* Clear command line buffer. */
	vty->cp = vty->length = 0;
	vty_clear_buf(vty);

	if (vty->status != VTY_CLOSE)
		vty_prompt(vty);

	return ret;
}

/* Send WILL TELOPT_ECHO to remote server. */
static void
vty_will_echo (struct vty *vty)
{
	unsigned char cmd[] = { IAC, WILL, TELOPT_ECHO, '\0' };
	vty_out (vty, "%s", cmd);
}

/* Make suppress Go-Ahead telnet option. */
static void
vty_will_suppress_go_ahead (struct vty *vty)
{
	unsigned char cmd[] = { IAC, WILL, TELOPT_SGA, '\0' };
	vty_out (vty, "%s", cmd);
}

/* Make don't use linemode over telnet. */
static void
vty_dont_linemode (struct vty *vty)
{
	unsigned char cmd[] = { IAC, DONT, TELOPT_LINEMODE, '\0' };
	vty_out (vty, "%s", cmd);
}

/* Use window size. */
static void
vty_do_window_size (struct vty *vty)
{
	unsigned char cmd[] = { IAC, DO, TELOPT_NAWS, '\0' };
	vty_out (vty, "%s", cmd);
}

static void vty_kill_line_from_beginning(struct vty *);
static void vty_redraw_line(struct vty *);

/* Print command line history.  This function is called from
   vty_next_line and vty_previous_line. */
static void vty_history_print(struct vty *vty)
{
	int length;

	vty_kill_line_from_beginning(vty);

	/* Get previous line from history buffer */
	length = strlen(vty->hist[vty->hp]);
	memcpy(vty->buf, vty->hist[vty->hp], length);
	vty->cp = vty->length = length;

	/* Redraw current line */
	vty_redraw_line(vty);
}

/* Show next command line history. */
static void vty_next_line(struct vty *vty)
{
	int try_index;

	if (vty->hp == vty->hindex)
		return;

	/* Try is there history exist or not. */
	try_index = vty->hp;
	if (try_index == (VTY_MAXHIST - 1))
		try_index = 0;
	else
		try_index++;

	/* If there is not history return. */
	if (vty->hist[try_index] == NULL)
		return;
	else
		vty->hp = try_index;

	vty_history_print(vty);
}

/* Show previous command line history. */
static void vty_previous_line(struct vty *vty)
{
	int try_index;

	try_index = vty->hp;
	if (try_index == 0)
		try_index = VTY_MAXHIST - 1;
	else
		try_index--;

	if (vty->hist[try_index] == NULL)
		return;
	else
		vty->hp = try_index;

	vty_history_print(vty);
}

/* This function redraw all of the command line character. */
static void vty_redraw_line(struct vty *vty)
{
	vty_write(vty, vty->buf, vty->length);
	vty->cp = vty->length;
}

/* Forward word. */
static void vty_forward_word(struct vty *vty)
{
	while (vty->cp != vty->length && vty->buf[vty->cp] != ' ')
		vty_forward_char(vty);

	while (vty->cp != vty->length && vty->buf[vty->cp] == ' ')
		vty_forward_char(vty);
}

/* Backward word without skipping training space. */
static void vty_backward_pure_word(struct vty *vty)
{
	while (vty->cp > 0 && vty->buf[vty->cp - 1] != ' ')
		vty_backward_char(vty);
}

/* Backward word. */
static void vty_backward_word(struct vty *vty)
{
	while (vty->cp > 0 && vty->buf[vty->cp - 1] == ' ')
		vty_backward_char(vty);

	while (vty->cp > 0 && vty->buf[vty->cp - 1] != ' ')
		vty_backward_char(vty);
}

/* When '^D' is typed at the beginning of the line we move to the down
   level. */
static void vty_down_level(struct vty *vty)
{
	vty_out(vty, "%s", VTY_NEWLINE);
	/* call the exit function of the specific node */
	if (vty->node > CONFIG_NODE)
		vty_go_parent(vty);
	else
		(*config_exit_cmd.func) (NULL, vty, 0, NULL);
	vty_prompt(vty);
	vty->cp = 0;
}

/* When '^Z' is received from vty, move down to the enable mode. */
static void vty_end_config(struct vty *vty)
{
	vty_out(vty, "%s", VTY_NEWLINE);

	/* FIXME: we need to call the exit function of the specific node
	 * in question, not this generic one that doesn't know all nodes */
	switch (vty->node) {
	case VIEW_NODE:
	case ENABLE_NODE:
		/* Nothing to do. */
		break;
	case CONFIG_NODE:
	case VTY_NODE:
		vty_config_unlock(vty);
		vty->node = ENABLE_NODE;
		break;
	case CFG_LOG_NODE:
		vty->node = CONFIG_NODE;
		break;
	default:
		/* Unknown node, we have to ignore it. */
		break;
	}

	vty_prompt(vty);
	vty->cp = 0;
}

/* Delete a charcter at the current point. */
static void vty_delete_char(struct vty *vty)
{
	int i;
	int size;

	if (vty->node == AUTH_NODE || vty->node == AUTH_ENABLE_NODE)
		return;

	if (vty->length == 0) {
		vty_down_level(vty);
		return;
	}

	if (vty->cp == vty->length)
		return;		/* completion need here? */

	size = vty->length - vty->cp;

	vty->length--;
	memmove(&vty->buf[vty->cp], &vty->buf[vty->cp + 1], size - 1);
	vty->buf[vty->length] = '\0';

	vty_write(vty, &vty->buf[vty->cp], size - 1);
	vty_write(vty, &telnet_space_char, 1);

	for (i = 0; i < size; i++)
		vty_write(vty, &telnet_backward_char, 1);
}

/* Delete a character before the point. */
static void vty_delete_backward_char(struct vty *vty)
{
	if (vty->cp == 0)
		return;

	vty_backward_char(vty);
	vty_delete_char(vty);
}

/* Kill rest of line from current point. */
static void vty_kill_line(struct vty *vty)
{
	int i;
	int size;

	size = vty->length - vty->cp;

	if (size == 0)
		return;

	for (i = 0; i < size; i++)
		vty_write(vty, &telnet_space_char, 1);
	for (i = 0; i < size; i++)
		vty_write(vty, &telnet_backward_char, 1);

	memset(&vty->buf[vty->cp], 0, size);
	vty->length = vty->cp;
}

/* Kill line from the beginning. */
static void vty_kill_line_from_beginning(struct vty *vty)
{
	vty_beginning_of_line(vty);
	vty_kill_line(vty);
}

/* Delete a word before the point. */
static void vty_forward_kill_word(struct vty *vty)
{
	while (vty->cp != vty->length && vty->buf[vty->cp] == ' ')
		vty_delete_char(vty);
	while (vty->cp != vty->length && vty->buf[vty->cp] != ' ')
		vty_delete_char(vty);
}

/* Delete a word before the point. */
static void vty_backward_kill_word(struct vty *vty)
{
	while (vty->cp > 0 && vty->buf[vty->cp - 1] == ' ')
		vty_delete_backward_char(vty);
	while (vty->cp > 0 && vty->buf[vty->cp - 1] != ' ')
		vty_delete_backward_char(vty);
}

/* Transpose chars before or at the point. */
static void vty_transpose_chars(struct vty *vty)
{
	char c1, c2;

	/* If length is short or point is near by the beginning of line then
	   return. */
	if (vty->length < 2 || vty->cp < 1)
		return;

	/* In case of point is located at the end of the line. */
	if (vty->cp == vty->length) {
		c1 = vty->buf[vty->cp - 1];
		c2 = vty->buf[vty->cp - 2];

		vty_backward_char(vty);
		vty_backward_char(vty);
		vty_self_insert_overwrite(vty, c1);
		vty_self_insert_overwrite(vty, c2);
	} else {
		c1 = vty->buf[vty->cp];
		c2 = vty->buf[vty->cp - 1];

		vty_backward_char(vty);
		vty_self_insert_overwrite(vty, c1);
		vty_self_insert_overwrite(vty, c2);
	}
}

/* Do completion at vty interface. */
static void vty_complete_command(struct vty *vty)
{
	int i;
	int ret;
	char **matched = NULL;
	vector vline;

	if (vty->node == AUTH_NODE || vty->node == AUTH_ENABLE_NODE)
		return;

	vline = cmd_make_strvec(vty->buf);
	if (vline == NULL)
		return;

	/* In case of 'help \t'. */
	if (isspace((int)vty->buf[vty->length - 1]))
		vector_set(vline, NULL);

	matched = cmd_complete_command(vline, vty, &ret);

	cmd_free_strvec(vline);

	vty_out(vty, "%s", VTY_NEWLINE);
	switch (ret) {
	case CMD_ERR_AMBIGUOUS:
		vty_out(vty, "%% Ambiguous command.%s", VTY_NEWLINE);
		vty_prompt(vty);
		vty_redraw_line(vty);
		break;
	case CMD_ERR_NO_MATCH:
		/* vty_out (vty, "%% There is no matched command.%s", VTY_NEWLINE); */
		vty_prompt(vty);
		vty_redraw_line(vty);
		break;
	case CMD_COMPLETE_FULL_MATCH:
		vty_prompt(vty);
		vty_redraw_line(vty);
		vty_backward_pure_word(vty);
		vty_insert_word_overwrite(vty, matched[0]);
		vty_self_insert(vty, ' ');
		talloc_free(matched[0]);
		break;
	case CMD_COMPLETE_MATCH:
		vty_prompt(vty);
		vty_redraw_line(vty);
		vty_backward_pure_word(vty);
		vty_insert_word_overwrite(vty, matched[0]);
		talloc_free(matched[0]);
		break;
	case CMD_COMPLETE_LIST_MATCH:
		for (i = 0; matched[i] != NULL; i++) {
			if (i != 0 && ((i % 6) == 0))
				vty_out(vty, "%s", VTY_NEWLINE);
			vty_out(vty, "%-10s ", matched[i]);
			talloc_free(matched[i]);
		}
		vty_out(vty, "%s", VTY_NEWLINE);

		vty_prompt(vty);
		vty_redraw_line(vty);
		break;
	case CMD_ERR_NOTHING_TODO:
		vty_prompt(vty);
		vty_redraw_line(vty);
		break;
	default:
		break;
	}
	if (matched)
		vector_only_index_free(matched);
}

static void
vty_describe_fold(struct vty *vty, int cmd_width,
		  unsigned int desc_width, struct desc *desc)
{
	char *buf;
	const char *cmd, *p;
	int pos;

	cmd = desc->cmd[0] == '.' ? desc->cmd + 1 : desc->cmd;

	if (desc_width <= 0) {
		vty_out(vty, "  %-*s  %s%s", cmd_width, cmd, desc->str,
			VTY_NEWLINE);
		return;
	}

	buf = _talloc_zero(vty, strlen(desc->str) + 1, "describe_fold");
	if (!buf)
		return;

	for (p = desc->str; strlen(p) > desc_width; p += pos + 1) {
		for (pos = desc_width; pos > 0; pos--)
			if (*(p + pos) == ' ')
				break;

		if (pos == 0)
			break;

		strncpy(buf, p, pos);
		buf[pos] = '\0';
		vty_out(vty, "  %-*s  %s%s", cmd_width, cmd, buf, VTY_NEWLINE);

		cmd = "";
	}

	vty_out(vty, "  %-*s  %s%s", cmd_width, cmd, p, VTY_NEWLINE);

	talloc_free(buf);
}

/* Describe matched command function. */
static void vty_describe_command(struct vty *vty)
{
	int ret;
	vector vline;
	vector describe;
	unsigned int i, width, desc_width;
	struct desc *desc, *desc_cr = NULL;

	vline = cmd_make_strvec(vty->buf);

	/* In case of '> ?'. */
	if (vline == NULL) {
		vline = vector_init(1);
		vector_set(vline, NULL);
	} else if (isspace((int)vty->buf[vty->length - 1]))
		vector_set(vline, NULL);

	describe = cmd_describe_command(vline, vty, &ret);

	vty_out(vty, "%s", VTY_NEWLINE);

	/* Ambiguous error. */
	switch (ret) {
	case CMD_ERR_AMBIGUOUS:
		cmd_free_strvec(vline);
		vty_out(vty, "%% Ambiguous command.%s", VTY_NEWLINE);
		vty_prompt(vty);
		vty_redraw_line(vty);
		return;
		break;
	case CMD_ERR_NO_MATCH:
		cmd_free_strvec(vline);
		vty_out(vty, "%% There is no matched command.%s", VTY_NEWLINE);
		vty_prompt(vty);
		vty_redraw_line(vty);
		return;
		break;
	}

	/* Get width of command string. */
	width = 0;
	for (i = 0; i < vector_active(describe); i++)
		if ((desc = vector_slot(describe, i)) != NULL) {
			unsigned int len;

			if (desc->cmd[0] == '\0')
				continue;

			len = strlen(desc->cmd);
			if (desc->cmd[0] == '.')
				len--;

			if (width < len)
				width = len;
		}

	/* Get width of description string. */
	desc_width = vty->width - (width + 6);

	/* Print out description. */
	for (i = 0; i < vector_active(describe); i++)
		if ((desc = vector_slot(describe, i)) != NULL) {
			if (desc->cmd[0] == '\0')
				continue;

			if (strcmp(desc->cmd, "<cr>") == 0) {
				desc_cr = desc;
				continue;
			}

			if (!desc->str)
				vty_out(vty, "  %-s%s",
					desc->cmd[0] ==
					'.' ? desc->cmd + 1 : desc->cmd,
					VTY_NEWLINE);
			else if (desc_width >= strlen(desc->str))
				vty_out(vty, "  %-*s  %s%s", width,
					desc->cmd[0] ==
					'.' ? desc->cmd + 1 : desc->cmd,
					desc->str, VTY_NEWLINE);
			else
				vty_describe_fold(vty, width, desc_width, desc);

#if 0
			vty_out(vty, "  %-*s %s%s", width
				desc->cmd[0] == '.' ? desc->cmd + 1 : desc->cmd,
				desc->str ? desc->str : "", VTY_NEWLINE);
#endif				/* 0 */
		}

	if ((desc = desc_cr)) {
		if (!desc->str)
			vty_out(vty, "  %-s%s",
				desc->cmd[0] == '.' ? desc->cmd + 1 : desc->cmd,
				VTY_NEWLINE);
		else if (desc_width >= strlen(desc->str))
			vty_out(vty, "  %-*s  %s%s", width,
				desc->cmd[0] == '.' ? desc->cmd + 1 : desc->cmd,
				desc->str, VTY_NEWLINE);
		else
			vty_describe_fold(vty, width, desc_width, desc);
	}

	cmd_free_strvec(vline);
	vector_free(describe);

	vty_prompt(vty);
	vty_redraw_line(vty);
}

/* ^C stop current input and do not add command line to the history. */
static void vty_stop_input(struct vty *vty)
{
	vty->cp = vty->length = 0;
	vty_clear_buf(vty);
	vty_out(vty, "%s", VTY_NEWLINE);

	switch (vty->node) {
	case VIEW_NODE:
	case ENABLE_NODE:
		/* Nothing to do. */
		break;
	case CONFIG_NODE:
	case VTY_NODE:
		vty_config_unlock(vty);
		vty->node = ENABLE_NODE;
		break;
	case CFG_LOG_NODE:
		vty->node = CONFIG_NODE;
		break;
	default:
		/* Unknown node, we have to ignore it. */
		break;
	}
	vty_prompt(vty);

	/* Set history pointer to the latest one. */
	vty->hp = vty->hindex;
}

#define CONTROL(X)  ((X) - '@')
#define VTY_NORMAL     0
#define VTY_PRE_ESCAPE 1
#define VTY_ESCAPE     2

/* Escape character command map. */
static void vty_escape_map(unsigned char c, struct vty *vty)
{
	switch (c) {
	case ('A'):
		vty_previous_line(vty);
		break;
	case ('B'):
		vty_next_line(vty);
		break;
	case ('C'):
		vty_forward_char(vty);
		break;
	case ('D'):
		vty_backward_char(vty);
		break;
	default:
		break;
	}

	/* Go back to normal mode. */
	vty->escape = VTY_NORMAL;
}

/* Quit print out to the buffer. */
static void vty_buffer_reset(struct vty *vty)
{
	buffer_reset(vty->obuf);
	vty_prompt(vty);
	vty_redraw_line(vty);
}

/*! \brief Read data via vty socket. */
int vty_read(struct vty *vty)
{
	int i;
	int nbytes;
	unsigned char buf[VTY_READ_BUFSIZ];

	int vty_sock = vty->fd;

	/* Read raw data from socket */
	if ((nbytes = read(vty->fd, buf, VTY_READ_BUFSIZ)) <= 0) {
		if (nbytes < 0) {
			if (ERRNO_IO_RETRY(errno)) {
				vty_event(VTY_READ, vty_sock, vty);
				return 0;
			}
		}
		buffer_reset(vty->obuf);
		vty->status = VTY_CLOSE;
	}

	for (i = 0; i < nbytes; i++) {
		if (buf[i] == IAC) {
			if (!vty->iac) {
				vty->iac = 1;
				continue;
			} else {
				vty->iac = 0;
			}
		}

		if (vty->iac_sb_in_progress && !vty->iac) {
			if (vty->sb_len < sizeof(vty->sb_buf))
				vty->sb_buf[vty->sb_len] = buf[i];
			vty->sb_len++;
			continue;
		}

		if (vty->iac) {
			/* In case of telnet command */
			int ret = 0;
			ret = vty_telnet_option(vty, buf + i, nbytes - i);
			vty->iac = 0;
			i += ret;
			continue;
		}

		if (vty->status == VTY_MORE) {
			switch (buf[i]) {
			case CONTROL('C'):
			case 'q':
			case 'Q':
				vty_buffer_reset(vty);
				break;
#if 0				/* More line does not work for "show ip bgp".  */
			case '\n':
			case '\r':
				vty->status = VTY_MORELINE;
				break;
#endif
			default:
				break;
			}
			continue;
		}

		/* Escape character. */
		if (vty->escape == VTY_ESCAPE) {
			vty_escape_map(buf[i], vty);
			continue;
		}

		/* Pre-escape status. */
		if (vty->escape == VTY_PRE_ESCAPE) {
			switch (buf[i]) {
			case '[':
				vty->escape = VTY_ESCAPE;
				break;
			case 'b':
				vty_backward_word(vty);
				vty->escape = VTY_NORMAL;
				break;
			case 'f':
				vty_forward_word(vty);
				vty->escape = VTY_NORMAL;
				break;
			case 'd':
				vty_forward_kill_word(vty);
				vty->escape = VTY_NORMAL;
				break;
			case CONTROL('H'):
			case 0x7f:
				vty_backward_kill_word(vty);
				vty->escape = VTY_NORMAL;
				break;
			default:
				vty->escape = VTY_NORMAL;
				break;
			}
			continue;
		}

		switch (buf[i]) {
		case CONTROL('A'):
			vty_beginning_of_line(vty);
			break;
		case CONTROL('B'):
			vty_backward_char(vty);
			break;
		case CONTROL('C'):
			vty_stop_input(vty);
			break;
		case CONTROL('D'):
			vty_delete_char(vty);
			break;
		case CONTROL('E'):
			vty_end_of_line(vty);
			break;
		case CONTROL('F'):
			vty_forward_char(vty);
			break;
		case CONTROL('H'):
		case 0x7f:
			vty_delete_backward_char(vty);
			break;
		case CONTROL('K'):
			vty_kill_line(vty);
			break;
		case CONTROL('N'):
			vty_next_line(vty);
			break;
		case CONTROL('P'):
			vty_previous_line(vty);
			break;
		case CONTROL('T'):
			vty_transpose_chars(vty);
			break;
		case CONTROL('U'):
			vty_kill_line_from_beginning(vty);
			break;
		case CONTROL('W'):
			vty_backward_kill_word(vty);
			break;
		case CONTROL('Z'):
			vty_end_config(vty);
			break;
		case '\n':
		case '\r':
			vty_out(vty, "%s", VTY_NEWLINE);
			vty_execute(vty);
			break;
		case '\t':
			vty_complete_command(vty);
			break;
		case '?':
			if (vty->node == AUTH_NODE
			    || vty->node == AUTH_ENABLE_NODE)
				vty_self_insert(vty, buf[i]);
			else
				vty_describe_command(vty);
			break;
		case '\033':
			if (i + 1 < nbytes && buf[i + 1] == '[') {
				vty->escape = VTY_ESCAPE;
				i++;
			} else
				vty->escape = VTY_PRE_ESCAPE;
			break;
		default:
			if (buf[i] > 31 && buf[i] < 127)
				vty_self_insert(vty, buf[i]);
			break;
		}
	}

	/* Check status. */
	if (vty->status == VTY_CLOSE) {
		vty_close(vty);
		return -EBADF;
	} else {
		vty_event(VTY_WRITE, vty_sock, vty);
		vty_event(VTY_READ, vty_sock, vty);
	}
	return 0;
}

/* Read up configuration file */
static int
vty_read_file(FILE *confp, void *priv)
{
	int ret;
	struct vty *vty;

	vty = vty_new();
	vty->fd = 0;
	vty->type = VTY_FILE;
	vty->node = CONFIG_NODE;
	vty->priv = priv;

	ret = config_from_file(vty, confp);

	if (ret != CMD_SUCCESS) {
		switch (ret) {
		case CMD_ERR_AMBIGUOUS:
			fprintf(stderr, "Ambiguous command.\n");
			break;
		case CMD_ERR_NO_MATCH:
			fprintf(stderr, "There is no such command.\n");
			break;
		}
		fprintf(stderr, "Error occurred during reading below "
			"line:\n%s\n", vty->buf);
		vty_close(vty);
		return -EINVAL;
	}

	vty_close(vty);
	return 0;
}

/*! \brief Create new vty structure. */
struct vty *
vty_create (int vty_sock, void *priv)
{
  struct vty *vty;

	struct termios t;

	tcgetattr(vty_sock, &t);
	cfmakeraw(&t);
	tcsetattr(vty_sock, TCSANOW, &t);

  /* Allocate new vty structure and set up default values. */
  vty = vty_new ();
  vty->fd = vty_sock;
  vty->priv = priv;
  vty->type = VTY_TERM;
  if (!password_check)
    {
      if (host.advanced)
	vty->node = ENABLE_NODE;
      else
	vty->node = VIEW_NODE;
    }
  else
    vty->node = AUTH_NODE;
  vty->fail = 0;
  vty->cp = 0;
  vty_clear_buf (vty);
  vty->length = 0;
  memset (vty->hist, 0, sizeof (vty->hist));
  vty->hp = 0;
  vty->hindex = 0;
  vector_set_index (vtyvec, vty_sock, vty);
  vty->status = VTY_NORMAL;
  if (host.lines >= 0)
    vty->lines = host.lines;
  else
    vty->lines = -1;

  if (password_check)
    {
      /* Vty is not available if password isn't set. */
      if (host.password == NULL && host.password_encrypt == NULL)
	{
	  vty_out (vty, "Vty password is not set.%s", VTY_NEWLINE);
	  vty->status = VTY_CLOSE;
	  vty_close (vty);
	  return NULL;
	}
    }

  /* Say hello to the world. */
  vty_hello (vty);
  if (password_check)
    vty_out (vty, "%sUser Access Verification%s%s", VTY_NEWLINE, VTY_NEWLINE, VTY_NEWLINE);

  /* Setting up terminal. */
  vty_will_echo (vty);
  vty_will_suppress_go_ahead (vty);

  vty_dont_linemode (vty);
  vty_do_window_size (vty);
  /* vty_dont_lflow_ahead (vty); */

  vty_prompt (vty);

  /* Add read/write thread. */
  vty_event (VTY_WRITE, vty_sock, vty);
  vty_event (VTY_READ, vty_sock, vty);

  return vty;
}

DEFUN(config_who, config_who_cmd, "who", "Display who is on vty\n")
{
	unsigned int i;
	struct vty *v;

	for (i = 0; i < vector_active(vtyvec); i++)
		if ((v = vector_slot(vtyvec, i)) != NULL)
			vty_out(vty, "%svty[%d] %s",
				v->config ? "*" : " ", i, VTY_NEWLINE);
	return CMD_SUCCESS;
}

/* Move to vty configuration mode. */
DEFUN(line_vty,
      line_vty_cmd,
      "line vty", "Configure a terminal line\n" "Virtual terminal\n")
{
	vty->node = VTY_NODE;
	return CMD_SUCCESS;
}

/* vty login. */
DEFUN(vty_login, vty_login_cmd, "login", "Enable password checking\n")
{
	password_check = 1;
	return CMD_SUCCESS;
}

DEFUN(no_vty_login,
      no_vty_login_cmd, "no login", NO_STR "Enable password checking\n")
{
	password_check = 0;
	return CMD_SUCCESS;
}

/* vty bind */
DEFUN(vty_bind, vty_bind_cmd, "bind A.B.C.D",
      "Accept VTY telnet connections on local interface\n"
      "Local interface IP address (default: " VTY_BIND_ADDR_DEFAULT ")\n")
{
	talloc_free((void*)vty_bind_addr);
	vty_bind_addr = talloc_strdup(tall_vty_ctx, argv[0]);
	return CMD_SUCCESS;
}

const char *vty_get_bind_addr(void)
{
	if (!vty_bind_addr)
		return VTY_BIND_ADDR_DEFAULT;
	return vty_bind_addr;
}

DEFUN(service_advanced_vty,
      service_advanced_vty_cmd,
      "service advanced-vty",
      "Set up miscellaneous service\n" "Enable advanced mode vty interface\n")
{
	host.advanced = 1;
	return CMD_SUCCESS;
}

DEFUN(no_service_advanced_vty,
      no_service_advanced_vty_cmd,
      "no service advanced-vty",
      NO_STR
      "Set up miscellaneous service\n" "Enable advanced mode vty interface\n")
{
	host.advanced = 0;
	return CMD_SUCCESS;
}

DEFUN(terminal_monitor,
      terminal_monitor_cmd,
      "terminal monitor",
      "Set terminal line parameters\n"
      "Copy debug output to the current terminal line\n")
{
	vty->monitor = 1;
	return CMD_SUCCESS;
}

DEFUN(terminal_no_monitor,
      terminal_no_monitor_cmd,
      "terminal no monitor",
      "Set terminal line parameters\n"
      NO_STR "Copy debug output to the current terminal line\n")
{
	vty->monitor = 0;
	return CMD_SUCCESS;
}

DEFUN(show_history,
      show_history_cmd,
      "show history", SHOW_STR "Display the session command history\n")
{
	int index;

	for (index = vty->hindex + 1; index != vty->hindex;) {
		if (index == VTY_MAXHIST) {
			index = 0;
			continue;
		}

		if (vty->hist[index] != NULL)
			vty_out(vty, "  %s%s", vty->hist[index], VTY_NEWLINE);

		index++;
	}

	return CMD_SUCCESS;
}

/* Display current configuration. */
static int vty_config_write(struct vty *vty)
{
	vty_out(vty, "line vty%s", VTY_NEWLINE);

	/* login */
	if (!password_check)
		vty_out(vty, " no login%s", VTY_NEWLINE);

	/* bind */
	if (vty_bind_addr && (strcmp(vty_bind_addr, "127.0.0.1") != 0))
		vty_out(vty, " bind %s%s", vty_bind_addr, VTY_NEWLINE);

	vty_out(vty, "!%s", VTY_NEWLINE);

	return CMD_SUCCESS;
}

struct cmd_node vty_node = {
	VTY_NODE,
	"%s(config-line)# ",
	1,
};

/*! \brief Reset all VTY status. */
void vty_reset(void)
{
	unsigned int i;
	struct vty *vty;
	struct thread *vty_serv_thread;

	for (i = 0; i < vector_active(vtyvec); i++)
		if ((vty = vector_slot(vtyvec, i)) != NULL) {
			buffer_reset(vty->obuf);
			vty->status = VTY_CLOSE;
			vty_close(vty);
		}

	for (i = 0; i < vector_active(Vvty_serv_thread); i++)
		if ((vty_serv_thread =
		     vector_slot(Vvty_serv_thread, i)) != NULL) {
			//thread_cancel (vty_serv_thread);
			vector_slot(Vvty_serv_thread, i) = NULL;
			close(i);
		}
}

static void vty_save_cwd(void)
{
	char cwd[MAXPATHLEN];
	char *c ;

	c = getcwd(cwd, MAXPATHLEN);

	if (!c) {
		if (chdir(SYSCONFDIR) != 0)
		    perror("chdir failed");
		if (getcwd(cwd, MAXPATHLEN) == NULL)
		    perror("getcwd failed");
	}

	vty_cwd = _talloc_zero(tall_vty_ctx, strlen(cwd) + 1, "save_cwd");
	strcpy(vty_cwd, cwd);
}

char *vty_get_cwd(void)
{
	return vty_cwd;
}

int vty_shell_serv(struct vty *vty)
{
	return vty->type == VTY_SHELL_SERV ? 1 : 0;
}

void vty_init_vtysh(void)
{
	vtyvec = vector_init(VECTOR_MIN_SIZE);
}

extern void *tall_bsc_ctx;

/*! \brief Initialize VTY layer
 *  \param[in] app_info application information
 */
/* Install vty's own commands like `who' command. */
void vty_init(struct vty_app_info *app_info)
{
	tall_vty_ctx = talloc_named_const(app_info->tall_ctx, 0, "vty");
	tall_vty_vec_ctx = talloc_named_const(tall_vty_ctx, 0, "vty_vector");
	tall_vty_cmd_ctx = talloc_named_const(tall_vty_ctx, 0, "vty_command");

	cmd_init(1);

	host.app_info = app_info;

	/* For further configuration read, preserve current directory. */
	vty_save_cwd();

	vtyvec = vector_init(VECTOR_MIN_SIZE);

	/* Install bgp top node. */
	install_node(&vty_node, vty_config_write);

	install_element_ve(&config_who_cmd);
	install_element_ve(&show_history_cmd);
	install_element(CONFIG_NODE, &line_vty_cmd);
	install_element(CONFIG_NODE, &service_advanced_vty_cmd);
	install_element(CONFIG_NODE, &no_service_advanced_vty_cmd);
	install_element(CONFIG_NODE, &show_history_cmd);
	install_element(ENABLE_NODE, &terminal_monitor_cmd);
	install_element(ENABLE_NODE, &terminal_no_monitor_cmd);

	vty_install_default(VTY_NODE);
	install_element(VTY_NODE, &vty_login_cmd);
	install_element(VTY_NODE, &no_vty_login_cmd);
	install_element(VTY_NODE, &vty_bind_cmd);
}

/*! \brief Read the configuration file using the VTY code
 *  \param[in] file_name file name of the configuration file
 *  \param[in] priv private data to be passed to \ref vty_read_file
 */
int vty_read_config_file(const char *file_name, void *priv)
{
	FILE *cfile;
	int rc;

	cfile = fopen(file_name, "r");
	if (!cfile)
		return -ENOENT;

	rc = vty_read_file(cfile, priv);
	fclose(cfile);

	host_config_set(file_name);

	return rc;
}

/*! @} */
