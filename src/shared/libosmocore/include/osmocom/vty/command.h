/*
 * Zebra configuration command interface routine
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2, or (at your
 * option) any later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _ZEBRA_COMMAND_H
#define _ZEBRA_COMMAND_H

#include <stdio.h>
#include <sys/types.h>
#include "vector.h"

/*! \defgroup command VTY Command
 *  @{
 */
/*! \file command.h */

/*! \brief Host configuration variable */
struct host {
	/*! \brief Host name of this router. */
	char *name;

	/*! \brief Password for vty interface. */
	char *password;
	char *password_encrypt;

	/*! \brief Enable password */
	char *enable;
	char *enable_encrypt;

	/*! \brief System wide terminal lines. */
	int lines;

	/*! \brief Log filename. */
	char *logfile;

	/*! \brief config file name of this host */
	char *config;

	/*! \brief Flags for services */
	int advanced;
	int encrypt;

	/*! \brief Banner configuration. */
	const char *motd;
	char *motdfile;

	/*! \brief VTY application information */
	const struct vty_app_info *app_info;
};

/*! \brief There are some command levels which called from command node. */
enum node_type {
	AUTH_NODE,		/*!< \brief Authentication mode of vty interface. */
	VIEW_NODE,		/*!< \brief View node. Default mode of vty interface. */
	AUTH_ENABLE_NODE,	/*!< \brief Authentication mode for change enable. */
	ENABLE_NODE,		/*!< \brief Enable node. */
	CONFIG_NODE,		/*!< \brief Config node. Default mode of config file. */
	SERVICE_NODE,		/*!< \brief Service node. */
	DEBUG_NODE,		/*!< \brief Debug node. */
	CFG_LOG_NODE,		/*!< \brief Configure the logging */

	VTY_NODE,		/*!< \brief Vty node. */

	L_E1INP_NODE,		/*!< \brief E1 line in libosmo-abis. */
	L_IPA_NODE,		/*!< \brief IPA proxying commands in libosmo-abis. */
	L_NS_NODE,		/*!< \brief NS node in libosmo-gb. */
	L_BSSGP_NODE,		/*!< \brief BSSGP node in libosmo-gb. */

	_LAST_OSMOVTY_NODE
};

#include "vty.h"

/*! \brief Node which has some commands and prompt string and
 * configuration function pointer . */
struct cmd_node {
	/*! \brief Node index */
	enum node_type node;

	/*! \brief Prompt character at vty interface. */
	const char *prompt;

	/*! \brief Is this node's configuration goes to vtysh ? */
	int vtysh;

	/*! \brief Node's configuration write function */
	int (*func) (struct vty *);

	/*! \brief Vector of this node's command list. */
	vector cmd_vector;
};

enum {
	CMD_ATTR_DEPRECATED = 1,
	CMD_ATTR_HIDDEN,
};

/*! \brief Structure of a command element */
struct cmd_element {
	const char *string;	/*!< \brief Command specification by string. */
	int (*func) (struct cmd_element *, struct vty *, int, const char *[]);
	const char *doc;	/*!< \brief Documentation of this command. */
	int daemon;		/*!< \brief Daemon to which this command belong. */
	vector strvec;		/*!< \brief Pointing out each description vector. */
	unsigned int cmdsize;	/*!< \brief Command index count. */
	char *config;		/*!< \brief Configuration string */
	vector subconfig;	/*!< \brief Sub configuration string */
	u_char attr;		/*!< \brief Command attributes */
};

/*! \brief Command description structure. */
struct desc {
	const char *cmd;	/*!< \brief Command string. */
	const char *str;	/*!< \brief Command's description. */
};

/*! \brief Return value of the commands. */
#define CMD_SUCCESS              0
#define CMD_WARNING              1
#define CMD_ERR_NO_MATCH         2
#define CMD_ERR_AMBIGUOUS        3
#define CMD_ERR_INCOMPLETE       4
#define CMD_ERR_EXEED_ARGC_MAX   5
#define CMD_ERR_NOTHING_TODO     6
#define CMD_COMPLETE_FULL_MATCH  7
#define CMD_COMPLETE_MATCH       8
#define CMD_COMPLETE_LIST_MATCH  9
#define CMD_SUCCESS_DAEMON      10

/* Argc max counts. */
#define CMD_ARGC_MAX   256

/* Turn off these macros when uisng cpp with extract.pl */
#ifndef VTYSH_EXTRACT_PL

/* helper defines for end-user DEFUN* macros */
#define DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, attrs, dnum) \
  static struct cmd_element cmdname = \
  { \
    .string = cmdstr, \
    .func = funcname, \
    .doc = helpstr, \
    .attr = attrs, \
    .daemon = dnum, \
  };

/* global (non static) cmd_element */
#define gDEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, attrs, dnum) \
  struct cmd_element cmdname = \
  { \
    .string = cmdstr, \
    .func = funcname, \
    .doc = helpstr, \
    .attr = attrs, \
    .daemon = dnum, \
  };

#define DEFUN_CMD_FUNC_DECL(funcname) \
  static int funcname (struct cmd_element *, struct vty *, int, const char *[]); \

#define DEFUN_CMD_FUNC_TEXT(funcname) \
  static int funcname \
    (struct cmd_element *self, struct vty *vty, int argc, const char *argv[])

/*! \brief Macro for defining a VTY node and function
 *  \param[in] funcname Name of the function implementing the node
 *  \param[in] cmdname Name of the command node
 *  \param[in] cmdstr String with syntax of node
 *  \param[in] helpstr String with help message of node
 */
#define DEFUN(funcname, cmdname, cmdstr, helpstr) \
  DEFUN_CMD_FUNC_DECL(funcname) \
  DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, 0, 0) \
  DEFUN_CMD_FUNC_TEXT(funcname)

/*! \brief Macro for defining a non-static (global) VTY node and function
 *  \param[in] funcname Name of the function implementing the node
 *  \param[in] cmdname Name of the command node
 *  \param[in] cmdstr String with syntax of node
 *  \param[in] helpstr String with help message of node
 */
#define gDEFUN(funcname, cmdname, cmdstr, helpstr) \
  DEFUN_CMD_FUNC_DECL(funcname) \
  gDEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, 0, 0) \
  DEFUN_CMD_FUNC_TEXT(funcname)

#define DEFUN_ATTR(funcname, cmdname, cmdstr, helpstr, attr) \
  DEFUN_CMD_FUNC_DECL(funcname) \
  DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, attr, 0) \
  DEFUN_CMD_FUNC_TEXT(funcname)

#define DEFUN_HIDDEN(funcname, cmdname, cmdstr, helpstr) \
  DEFUN_ATTR (funcname, cmdname, cmdstr, helpstr, CMD_ATTR_HIDDEN)

#define DEFUN_DEPRECATED(funcname, cmdname, cmdstr, helpstr) \
  DEFUN_ATTR (funcname, cmdname, cmdstr, helpstr, CMD_ATTR_DEPRECATED) \

/* DEFUN_NOSH for commands that vtysh should ignore */
#define DEFUN_NOSH(funcname, cmdname, cmdstr, helpstr) \
  DEFUN(funcname, cmdname, cmdstr, helpstr)

/* DEFSH for vtysh. */
#define DEFSH(daemon, cmdname, cmdstr, helpstr) \
  DEFUN_CMD_ELEMENT(NULL, cmdname, cmdstr, helpstr, 0, daemon) \

/* DEFUN + DEFSH */
#define DEFUNSH(daemon, funcname, cmdname, cmdstr, helpstr) \
  DEFUN_CMD_FUNC_DECL(funcname) \
  DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, 0, daemon) \
  DEFUN_CMD_FUNC_TEXT(funcname)

/* DEFUN + DEFSH with attributes */
#define DEFUNSH_ATTR(daemon, funcname, cmdname, cmdstr, helpstr, attr) \
  DEFUN_CMD_FUNC_DECL(funcname) \
  DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, attr, daemon) \
  DEFUN_CMD_FUNC_TEXT(funcname)

#define DEFUNSH_HIDDEN(daemon, funcname, cmdname, cmdstr, helpstr) \
  DEFUNSH_ATTR (daemon, funcname, cmdname, cmdstr, helpstr, CMD_ATTR_HIDDEN)

#define DEFUNSH_DEPRECATED(daemon, funcname, cmdname, cmdstr, helpstr) \
  DEFUNSH_ATTR (daemon, funcname, cmdname, cmdstr, helpstr, CMD_ATTR_DEPRECATED)

/* ALIAS macro which define existing command's alias. */
#define ALIAS(funcname, cmdname, cmdstr, helpstr) \
  DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, 0, 0)

/* global (non static) cmd_element */
#define gALIAS(funcname, cmdname, cmdstr, helpstr) \
  gDEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, 0, 0)

#define ALIAS_ATTR(funcname, cmdname, cmdstr, helpstr, attr) \
  DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, attr, 0)

#define ALIAS_HIDDEN(funcname, cmdname, cmdstr, helpstr) \
  DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, CMD_ATTR_HIDDEN, 0)

#define ALIAS_DEPRECATED(funcname, cmdname, cmdstr, helpstr) \
  DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, CMD_ATTR_DEPRECATED, 0)

#define ALIAS_SH(daemon, funcname, cmdname, cmdstr, helpstr) \
  DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, 0, daemon)

#define ALIAS_SH_HIDDEN(daemon, funcname, cmdname, cmdstr, helpstr) \
  DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, CMD_ATTR_HIDDEN, daemon)

#define ALIAS_SH_DEPRECATED(daemon, funcname, cmdname, cmdstr, helpstr) \
  DEFUN_CMD_ELEMENT(funcname, cmdname, cmdstr, helpstr, CMD_ATTR_DEPRECATED, daemon)

#endif				/* VTYSH_EXTRACT_PL */

/* Some macroes */
#define CMD_OPTION(S)   ((S[0]) == '[')
#define CMD_VARIABLE(S) (((S[0]) >= 'A' && (S[0]) <= 'Z') || ((S[0]) == '<'))
#define CMD_VARARG(S)   ((S[0]) == '.')
#define CMD_RANGE(S)	((S[0] == '<'))

#define CMD_IPV4(S)	   ((strcmp ((S), "A.B.C.D") == 0))
#define CMD_IPV4_PREFIX(S) ((strcmp ((S), "A.B.C.D/M") == 0))
#define CMD_IPV6(S)        ((strcmp ((S), "X:X::X:X") == 0))
#define CMD_IPV6_PREFIX(S) ((strcmp ((S), "X:X::X:X/M") == 0))

/* Common descriptions. */
#define SHOW_STR "Show running system information\n"
#define IP_STR "IP information\n"
#define IPV6_STR "IPv6 information\n"
#define NO_STR "Negate a command or set its defaults\n"
#define CLEAR_STR "Reset functions\n"
#define RIP_STR "RIP information\n"
#define BGP_STR "BGP information\n"
#define OSPF_STR "OSPF information\n"
#define NEIGHBOR_STR "Specify neighbor router\n"
#define DEBUG_STR "Debugging functions (see also 'undebug')\n"
#define UNDEBUG_STR "Disable debugging functions (see also 'debug')\n"
#define ROUTER_STR "Enable a routing process\n"
#define AS_STR "AS number\n"
#define MBGP_STR "MBGP information\n"
#define MATCH_STR "Match values from routing table\n"
#define SET_STR "Set values in destination routing protocol\n"
#define OUT_STR "Filter outgoing routing updates\n"
#define IN_STR  "Filter incoming routing updates\n"
#define V4NOTATION_STR "specify by IPv4 address notation(e.g. 0.0.0.0)\n"
#define OSPF6_NUMBER_STR "Specify by number\n"
#define INTERFACE_STR "Interface infomation\n"
#define IFNAME_STR "Interface name(e.g. ep0)\n"
#define IP6_STR "IPv6 Information\n"
#define OSPF6_STR "Open Shortest Path First (OSPF) for IPv6\n"
#define OSPF6_ROUTER_STR "Enable a routing process\n"
#define OSPF6_INSTANCE_STR "<1-65535> Instance ID\n"
#define SECONDS_STR "<1-65535> Seconds\n"
#define ROUTE_STR "Routing Table\n"
#define PREFIX_LIST_STR "Build a prefix list\n"
#define OSPF6_DUMP_TYPE_LIST \
"(neighbor|interface|area|lsa|zebra|config|dbex|spf|route|lsdb|redistribute|hook|asbr|prefix|abr)"
#define ISIS_STR "IS-IS information\n"
#define AREA_TAG_STR "[area tag]\n"

#define CONF_BACKUP_EXT ".sav"

/* IPv4 only machine should not accept IPv6 address for peer's IP
   address.  So we replace VTY command string like below. */
#ifdef HAVE_IPV6
#define NEIGHBOR_CMD       "neighbor (A.B.C.D|X:X::X:X) "
#define NO_NEIGHBOR_CMD    "no neighbor (A.B.C.D|X:X::X:X) "
#define NEIGHBOR_ADDR_STR  "Neighbor address\nIPv6 address\n"
#define NEIGHBOR_CMD2      "neighbor (A.B.C.D|X:X::X:X|WORD) "
#define NO_NEIGHBOR_CMD2   "no neighbor (A.B.C.D|X:X::X:X|WORD) "
#define NEIGHBOR_ADDR_STR2 "Neighbor address\nNeighbor IPv6 address\nNeighbor tag\n"
#else
#define NEIGHBOR_CMD       "neighbor A.B.C.D "
#define NO_NEIGHBOR_CMD    "no neighbor A.B.C.D "
#define NEIGHBOR_ADDR_STR  "Neighbor address\n"
#define NEIGHBOR_CMD2      "neighbor (A.B.C.D|WORD) "
#define NO_NEIGHBOR_CMD2   "no neighbor (A.B.C.D|WORD) "
#define NEIGHBOR_ADDR_STR2 "Neighbor address\nNeighbor tag\n"
#endif				/* HAVE_IPV6 */

/* Prototypes. */
void install_node(struct cmd_node *, int (*)(struct vty *));
void install_default(enum node_type);
void install_element(enum node_type, struct cmd_element *);
void install_element_ve(struct cmd_element *cmd);
void sort_node(void);

/* Concatenates argv[shift] through argv[argc-1] into a single NUL-terminated
   string with a space between each element (allocated using
   XMALLOC(MTYPE_TMP)).  Returns NULL if shift >= argc. */
char *argv_concat(const char **argv, int argc, int shift);

vector cmd_make_strvec(const char *);
void cmd_free_strvec(vector);
vector cmd_describe_command();
char **cmd_complete_command();
const char *cmd_prompt(enum node_type);
int config_from_file(struct vty *, FILE *);
enum node_type node_parent(enum node_type);
int cmd_execute_command(vector, struct vty *, struct cmd_element **, int);
int cmd_execute_command_strict(vector, struct vty *, struct cmd_element **);
void config_replace_string(struct cmd_element *, char *, ...);
void cmd_init(int);

/* Export typical functions. */
extern struct cmd_element config_exit_cmd;
extern struct cmd_element config_help_cmd;
extern struct cmd_element config_list_cmd;
extern struct cmd_element config_end_cmd;
char *host_config_file();
void host_config_set(const char *);

/* This is called from main when a daemon is invoked with -v or --version. */
void print_version(int print_copyright);

extern void *tall_vty_cmd_ctx;

/*! @} */
#endif				/* _ZEBRA_COMMAND_H */
