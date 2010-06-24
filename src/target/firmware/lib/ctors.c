
/* iterate over list of constructor functions and call each element */
void do_global_ctors(const char *ctors_start, const char *ctors_end)
{
	typedef void (*func_ptr)(void);
	func_ptr *func;

	for (func = (func_ptr *) ctors_start;
	     *func && (func != (func_ptr *) ctors_end); func++)
		(*func)();
}
