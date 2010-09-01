
/****** MESSAGING MENU ******/

struct menu menu_message_compose = {
	.title = "Compose",
	.help = "Write a new text message."
};

struct menu menu_message_inbox = {
	.title = "Inbox",
	.help = "Incoming text messages"
};

struct menu menu_message_outbox = {
	.title = "Outbox",
	.help = "Outgoing text messages"
};

struct menu menu_message_sent = {
	.title = "Sent",
	.help = "Previously sent text messages"
};

struct menu menu_messages = {
	.title = "Messages",
	.help = "Short message service options",
	.children = {
		[0] = &menu_message_compose,
		[1] = &menu_message_inbox,
		[2] = &menu_message_outbox,
		[3] = &menu_message_sent
	}
};

/****** NETWORK MENU ******/

struct menu menu_network_about = {
	.title = "About this network",
	.help = "Information about your current network",
};

struct menu menu_network = {
	.title = "Network",
	.help = "Network interaction options",
	.children = {
	}
};

/****** SETTINGS MENU ******/

struct menu menu_settings = {
	.title = "Settings",
	.help = "Configure your phone",
	.children = {
	}
};

/****** MAIN MENU ******/

struct menu menu_about = {
	.title = "About",
	.help = "Information about this phone",
};

struct menu menu_main = {
	.title = "Main Menu",
	.children = {
		[0] = &menu_messages,
		[7] = &menu_network,
		[8] = &menu_settings,
		[9] = &menu_about,
	},
};



int
main(void) {
	&menu_main;
	return 0;
};
