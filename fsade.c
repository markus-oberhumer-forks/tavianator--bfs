/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2019 Tavian Barnes <tavianator@tavianator.com>             *
 *                                                                          *
 * Permission to use, copy, modify, and/or distribute this software for any *
 * purpose with or without fee is hereby granted.                           *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES *
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF         *
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR  *
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES   *
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN    *
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF  *
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.           *
 ****************************************************************************/

#include "fsade.h"
#include "bftw.h"
#include "dstring.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
#include <unistd.h>

#if BFS_CAN_CHECK_ACL
#	include <sys/acl.h>
#endif

#if BFS_CAN_CHECK_CAPABILITIES
#	include <sys/capability.h>
#endif

#if BFS_CAN_CHECK_XATTRS
#	include <sys/xattr.h>
#endif

#if BFS_CAN_CHECK_ACL || BFS_CAN_CHECK_CAPABILITIES || BFS_CAN_CHECK_XATTRS

/**
 * Many of the APIs used here don't have *at() variants, but we can try to
 * emulate something similar if /proc/self/fd is available.
 */
static const char *fake_at(const struct BFTW *ftwbuf) {
	static bool proc_works = true;
	static bool proc_checked = false;

	char *path = NULL;
	if (!proc_works || ftwbuf->at_fd == AT_FDCWD) {
		goto fail;
	}

	path = dstrprintf("/proc/self/fd/%d/", ftwbuf->at_fd);
	if (!path) {
		goto fail;
	}

	if (!proc_checked) {
		proc_checked = true;
		if (xfaccessat(AT_FDCWD, path, F_OK) != 0) {
			proc_works = false;
			goto fail;
		}
	}

	if (dstrcat(&path, ftwbuf->at_path) != 0) {
		goto fail;
	}

	return path;

fail:
	dstrfree(path);
	return ftwbuf->path;
}

static void free_fake_at(const struct BFTW *ftwbuf, const char *path) {
	if (path != ftwbuf->path) {
		dstrfree((char *)path);
	}
}

/**
 * Check if an error was caused by the absence of support or data for a feature.
 */
static bool is_absence_error(int error) {
	// If the OS doesn't support the feature, it's obviously not enabled for
	// any files
	if (error == ENOTSUP) {
		return true;
	}

	// On Linux, ACLs and capabilities are implemented in terms of extended
	// attributes, which report ENODATA/ENOATTR when missing

#ifdef ENODATA
	if (error == ENODATA) {
		return true;
	}
#endif

#if defined(ENOATTR) && ENOATTR != ENODATA
	if (error == ENOATTR) {
		return true;
	}
#endif

	return false;
}

#endif // BFS_CAN_CHECK_ACL || BFS_CAN_CHECK_CAPABILITIES || BFS_CAN_CHECK_XATTRS

#if BFS_CAN_CHECK_ACL

/** Check if any ACLs of the given type are non-trivial. */
static int bfs_check_acl_type(const char *path, acl_type_t type) {
	acl_t acl = acl_get_file(path, type);
	if (!acl) {
		if (is_absence_error(errno)) {
			return 0;
		} else {
			return -1;
		}
	}

	int ret = 0;
	acl_entry_t entry;
	for (int status = acl_get_entry(acl, ACL_FIRST_ENTRY, &entry);
	     status > 0;
	     status = acl_get_entry(acl, ACL_NEXT_ENTRY, &entry)) {
#if defined(ACL_USER_OBJ) && defined(ACL_GROUP_OBJ) && defined(ACL_OTHER)
		acl_tag_t tag;
		if (acl_get_tag_type(entry, &tag) != 0) {
			continue;
		}
		if (tag != ACL_USER_OBJ && tag != ACL_GROUP_OBJ && tag != ACL_OTHER) {
			ret = 1;
			break;
		}
#else
		ret = 1;
		break;
#endif
	}

	acl_free(acl);
	return ret;
}

int bfs_check_acl(const struct BFTW *ftwbuf) {
	if (ftwbuf->typeflag == BFTW_LNK) {
		return 0;
	}

	const char *path = fake_at(ftwbuf);

	int error = 0;
	int ret = bfs_check_acl_type(path, ACL_TYPE_ACCESS);
	if (ret < 0) {
		error = errno;
	}

	if (ret <= 0 && ftwbuf->typeflag == BFTW_DIR) {
		ret = bfs_check_acl_type(path, ACL_TYPE_DEFAULT);
		if (ret < 0) {
			error = errno;
		}
	}

#ifdef ACL_TYPE_EXTENDED
	if (ret <= 0) {
		ret = bfs_check_acl_type(path, ACL_TYPE_EXTENDED);
		if (ret < 0) {
			error = errno;
		}
	}
#endif

	free_fake_at(ftwbuf, path);
	errno = error;
	return ret;
}

#else // !BFS_CAN_CHECK_ACL

int bfs_check_acl(const struct BFTW *ftwbuf) {
	errno = ENOTSUP;
	return -1;
}

#endif

#if BFS_CAN_CHECK_CAPABILITIES

int bfs_check_capabilities(const struct BFTW *ftwbuf) {
	if (ftwbuf->typeflag == BFTW_LNK) {
		return 0;
	}

	int ret = -1, error;
	const char *path = fake_at(ftwbuf);

	cap_t caps = cap_get_file(path);
	if (!caps) {
		error = errno;
		if (is_absence_error(error)) {
			ret = 0;
		}
		goto out_path;
	}

	// TODO: Any better way to check for a non-empty capability set?
	char *text = cap_to_text(caps, NULL);
	if (!text) {
		error = errno;
		goto out_caps;
	}
	ret = text[0] ? 1 : 0;

	error = errno;
	cap_free(text);
out_caps:
	cap_free(caps);
out_path:
	free_fake_at(ftwbuf, path);
	errno = error;
	return ret;
}

#else // !BFS_CAN_CHECK_CAPABILITIES

int bfs_check_capabilities(const struct BFTW *ftwbuf) {
	errno = ENOTSUP;
	return -1;
}

#endif

#if BFS_CAN_CHECK_XATTRS

int bfs_check_xattrs(const struct BFTW *ftwbuf) {
	const char *path = fake_at(ftwbuf);
	ssize_t len;

#if __APPLE__
	int options = ftwbuf->typeflag == BFTW_LNK ? XATTR_NOFOLLOW : 0;
	len = listxattr(path, NULL, 0, options);
#else
	if (ftwbuf->typeflag == BFTW_LNK) {
		len = llistxattr(path, NULL, 0);
	} else {
		len = listxattr(path, NULL, 0);
	}
#endif

	int error = errno;

	free_fake_at(ftwbuf, path);

	if (len > 0) {
		return 1;
	} else if (len == 0 || is_absence_error(error)) {
		return 0;
	} else if (error == E2BIG) {
		return 1;
	} else {
		errno = error;
		return -1;
	}
}

#else // !BFS_CAN_CHECK_XATTRS

int bfs_check_xattrs(const struct BFTW *ftwbuf) {
	errno = ENOTSUP;
	return -1;
}

#endif
