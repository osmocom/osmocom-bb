
/**
 * Screens - full-screen dialogs
 *
 * These compose the first level of interaction in the UI.
 *
 * There is always exactly one active screen, which is in
 * control of the entire display on which it is displayed.
 *
 * Screen activations are stacked, providing interaction depth.
 *
 */
struct screen {
	const char *name;
	void (*on_enter)(void);
	void (*on_leave)(void);
	void (*on_render)(void);
	void (*on_key_press)(void);
	void (*on_key_release)(void);
};

