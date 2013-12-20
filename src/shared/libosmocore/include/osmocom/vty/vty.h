#ifndef _VTY_H
#define _VTY_H

#include <stdio.h>
#include <stdarg.h>

/*! \defgroup vty VTY (Virtual TTY) interface
 *  @{
 */
/*! \file vty.h */

/* GCC have printf type attribute check.  */
#ifdef __GNUC__
#define VTY_PRINTF_ATTRIBUTE(a,b) __attribute__ ((__format__ (__printf__, a, b)))
#else
#define VTY_PRINTF_ATTRIBUTE(a,b)
#endif				/* __GNUC__ */

/* Does the I/O error indicate that the operation should be retried later? */
#define ERRNO_IO_RETRY(EN) \
	(((EN) == EAGAIN) || ((EN) == EWOULDBLOCK) || ((EN) == EINTR))

/* Vty read buffer size. */
#define VTY_READ_BUFSIZ 512

#define VTY_BUFSIZ 512
#define VTY_MAXHIST 20

/*! \brief VTY events */
enum event {
	VTY_SERV,
	VTY_READ,
	VTY_WRITE,
	VTY_CLOSED,
	VTY_TIMEOUT_RESET,
#ifdef VTYSH
	VTYSH_SERV,
	VTYSH_READ,
	VTYSH_WRITE
#endif				/* VTYSH */
};

enum vty_type {
	VTY_TERM,
	VTY_FILE,
	VTY_SHELL,
	VTY_SHELL_SERV
};

/*! Internal representation of a single VTY */
struct vty {
	/*! \brief underlying file (if any) */
	FILE *file;

	/*! \brief private data, specified by creator */
	void *priv;

	/*! \brief File descripter of this vty. */
	int fd;

	/*! \brief Is this vty connect to file or not */
	enum vty_type type;

	/*! \brief Node status of this vty */
	int node;

	/*! \brief Failure count */
	int fail;

	/*! \brief Output buffer. */
	struct buffer *obuf;

	/*! \brief Command input buffer */
	char *buf;

	/*! \brief Command cursor point */
	int cp;

	/*! \brief Command length */
	int length;

	/*! \brief Command max length. */
	int max;

	/*! \brief Histry of command */
	char *hist[VTY_MAXHIST];

	/*! \brief History lookup current point */
	int hp;

	/*! \brief History insert end point */
	int hindex;

	/*! \brief For current referencing point of interface, route-map,
	   access-list etc... */
	void *index;

	/*! \brief For multiple level index treatment such as key chain and key. */
	void *index_sub;

	/*! \brief For escape character. */
	unsigned char escape;

	/*! \brief Current vty status. */
	enum { VTY_NORMAL, VTY_CLOSE, VTY_MORE, VTY_MORELINE } status;

	/*! \brief IAC handling
	 *
	 * IAC handling: was the last character received the IAC
	 * (interpret-as-command) escape character (and therefore the next
	 * character will be the command code)?  Refer to Telnet RFC 854. */
	unsigned char iac;

	/*! \brief IAC SB (option subnegotiation) handling */
	unsigned char iac_sb_in_progress;
	/* At the moment, we care only about the NAWS (window size) negotiation,
	 * and that requires just a 5-character buffer (RFC 1073):
	 * <NAWS char> <16-bit width> <16-bit height> */
#define TELNET_NAWS_SB_LEN 5
	/*! \brief sub-negotiation buffer */
	unsigned char sb_buf[TELNET_NAWS_SB_LEN];
	/*! \brief How many subnegotiation characters have we received?  
	 *
	 * We just drop those that do not fit in the buffer. */
	size_t sb_len;

	/*! \brief Window width */
	int width;
	/*! \brief Widnow height */
	int height;

	/*! \brief Configure lines. */
	int lines;

	int monitor;

	/*! \brief In configure mode. */
	int config;
};

/* Small macro to determine newline is newline only or linefeed needed. */
#define VTY_NEWLINE  ((vty->type == VTY_TERM) ? "\r\n" : "\n")

static inline const char *vty_newline(struct vty *vty)
{
	return VTY_NEWLINE;
}

/*! Information an application registers with the VTY */
struct vty_app_info {
	/*! \brief name of the application */
	const char *name;
	/*! \brief version string of the application */
	const char *version;
	/*! \brief copyright string of the application */
	const char *copyright;
	/*! \brief \ref talloc context */
	void *tall_ctx;
	/*! \brief call-back for returning to parent n ode */
	enum node_type (*go_parent_cb)(struct vty *vty);
	/*! \brief call-back to determine if node is config node */
	int (*is_config_node)(struct vty *vty, int node);
};

/* Prototypes. */
void vty_init(struct vty_app_info *app_info);
int vty_read_config_file(const char *file_name, void *priv);
void vty_init_vtysh (void);
void vty_reset (void);
struct vty *vty_new (void);
struct vty *vty_create (int vty_sock, void *priv);
int vty_out (struct vty *, const char *, ...) VTY_PRINTF_ATTRIBUTE(2, 3);
int vty_out_newline(struct vty *);
int vty_read(struct vty *vty);
//void vty_time_print (struct vty *, int);
void vty_close (struct vty *);
char *vty_get_cwd (void);
void vty_log (const char *level, const char *proto, const char *fmt, va_list);
int vty_config_lock (struct vty *);
int vty_config_unlock (struct vty *);
int vty_shell (struct vty *);
int vty_shell_serv (struct vty *);
void vty_hello (struct vty *);
void *vty_current_index(struct vty *);
int vty_current_node(struct vty *vty);
enum node_type vty_go_parent(struct vty *vty);

extern void *tall_vty_ctx;

extern struct cmd_element cfg_description_cmd;
extern struct cmd_element cfg_no_description_cmd;

/*! @} */

#endif
