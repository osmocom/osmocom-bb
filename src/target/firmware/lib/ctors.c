
/* iterate over list of constructor functions and call each element */
void do_global_ctors(const char *_ctors_start, const char *ctors_end)
{
	typedef void (*func_ptr)(void);
	func_ptr *func, *ctors_start = (func_ptr *) _ctors_start;

	/* skip the first entry, as it contains the number of
	 * constructors which we don't use */
	ctors_start++;

	for (func = ctors_start;
	     *func && (func != (func_ptr *) ctors_end); func++)
		(*func)();
}
