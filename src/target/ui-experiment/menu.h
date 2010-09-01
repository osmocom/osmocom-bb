
/**
 * Menus - menus and menu items
 *
 * We represent both menus and menu items in a single structure.
 *
 * They share the properties of having a title as well as having
 * interaction callbacks such as on_select.
 *
 * Menus have a child item array that is indexed by menu position.
 * The position of items in this array is used for numeric menu navigation.
 *
 */
struct menu {
	const char *title;
	void (*on_select)(void);
	struct menu *children[10];
};
