/*
 * Compare strings:  s1>s2: >0  s1==s2: 0  s1<s2: <0
 */

int strcmp(const char *s1, const char *s2)
{
	while (*s1 == *s2++)
		if (*s1++=='\0')
			return(0);
	return(*s1 - *--s2);
}
