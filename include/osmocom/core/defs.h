#ifndef OSMOCORE_DEFS_H
#define OSMOCORE_DEFS_H

/*! \defgroup utils General-purpose utility functions
 *  @{
 */

/*! \file defs.h
 *  \brief General definitions that are meant to be included from header files.
 */

/*! \brief Check for gcc and version.
 *
 * \note Albeit glibc provides a features.h file that contains a similar
 *       definition (__GNUC_PREREQ), this definition has been copied from there
 *       to have it available with other libraries, too.
 *
 * \return != 0 iff gcc is used and it's version is at least maj.min.
 */
#if defined __GNUC__ && defined __GNUC_MINOR__
# define OSMO_GNUC_PREREQ(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
# define OSMO_GNUC_PREREQ(maj, min) 0
#endif

/*! \brief Set the deprecated attribute with a message.
 */
#if defined(__clang__)
# if __has_attribute(deprecated)
#   define _OSMO_HAS_ATTRIBUTE_DEPRECATED 1
# endif
# if __has_extension(attribute_deprecated_with_message)
#   define _OSMO_HAS_ATTRIBUTE_DEPRECATED_WITH_MESSAGE 1
# endif
#elif defined(__GNUC__)
# define _OSMO_HAS_ATTRIBUTE_DEPRECATED 1
# if OSMO_GNUC_PREREQ(4,5)
#   define _OSMO_HAS_ATTRIBUTE_DEPRECATED_WITH_MESSAGE 1
# endif
#endif

#if defined(_OSMO_HAS_ATTRIBUTE_DEPRECATED_WITH_MESSAGE)
# define OSMO_DEPRECATED(text)  __attribute__((__deprecated__(text)))
#elif defined(_OSMO_HAS_ATTRIBUTE_DEPRECATED)
# define OSMO_DEPRECATED(text)  __attribute__((__deprecated__))
#else
# define OSMO_DEPRECATED(text)
#endif

#undef _OSMO_HAS_ATTRIBUTE_DEPRECATED_WITH_MESSAGE
#undef _OSMO_HAS_ATTRIBUTE_DEPRECATED

/*! @} */

#endif
