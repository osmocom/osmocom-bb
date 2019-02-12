char *
index(p, ch)
	register char *p, ch;
{
	for (;; ++p) {
		if (*p == ch)
			return(p);
		if (!*p)
			return((char *)0);
	}
	/* NOTREACHED */
}
