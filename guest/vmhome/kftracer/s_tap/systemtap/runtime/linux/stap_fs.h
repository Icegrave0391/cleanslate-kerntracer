/* -*- linux-c -*-
 * Utility functions for handling file systems and mount namespaces.
 *
 * Copyright (C) 2023 by OpenResty Inc.
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef LINUX_STAP_FS_H
#define LINUX_STAP_FS_H

#include <linux/namei.h>
#include <linux/fs_struct.h>
#include <linux/nsproxy.h>
#include <linux/proc_ns.h>

static int _stp_target_mnt_ns_fd = -1;
static int _stp_orig_mnt_ns_fd = -1;
static bool _stp_fs_struct_unshared = false;

static inline bool
has_set_mnt_ns(void)
{
	static int cached_res = -1;
	if (cached_res != -1)
		return cached_res;
	cached_res =
		(kallsyms_proc_ns_file && kallsyms_unshare_nsproxy_namespaces
		    && kallsyms_switch_task_namespaces && kallsyms_free_nsproxy)
		;

	return cached_res;
}

static inline int
stap_set_mnt_ns(int fd)
{
#ifdef STAPCONF_NSSET_COMPLETE
	struct file *file;
	struct nsset nsset = {};
	struct ns_common *ns = NULL;
	int err = 0;
	struct task_struct *me;

	if (!kallsyms_proc_ns_file || !kallsyms_unshare_nsproxy_namespaces
		|| !kallsyms_switch_task_namespaces || !kallsyms_free_nsproxy)
	{
        /* NB do nothing; ignore the error when the kernel is too
         * old to support this. */
		return 0;
	}

	dbug_task(2, "using our own setns() impl instead of the syscall\n");

	me = current;

	file = fget(fd);
	if (!file)
		return -EBADF;

	/* unlike the setns() syscall, we don't allow non-proc ns file */
	if (!(ibt_wrapper(bool,
			  (*(proc_ns_file_fn) kallsyms_proc_ns_file)(file))))
		err = -EINVAL;

	if (unlikely(err))
		goto out;

	ns = get_proc_ns(file_inode(file));
	if (ns->ops->type != CLONE_NEWNS) {
		err = -EINVAL;
		goto out;
	}

	/* NB: Alas. we actually need to call create_nsproxy_namesapes() here
	 * but it is an internal symbol declared by 'static'. The
	 * unshare_nsproxy_namespaces() function is the closest symbol we can
	 * use; but it allocates a new mnt ns we don't need. */
	err = ibt_wrapper(int, (*(unshare_nsproxy_namespaces_fn)
		kallsyms_unshare_nsproxy_namespaces)(CLONE_NEWNS, &nsset.nsproxy,
						     NULL, NULL));
	if (unlikely(err)) {
		goto out;
	}

	if (unlikely(nsset.nsproxy == NULL)) {
		err = -ENOMEM;
		goto out;
    }

	nsset.cred = current_cred();
	if (unlikely(!nsset.cred)) {
		err = -ENOMEM;
		goto out0;
	}

	nsset.fs = me->fs;
	nsset.flags = CLONE_NEWNS;

	err = ns->ops->install(&nsset, ns);
	if (unlikely(!err)) {
		/* transfer ownership */
		void_ibt_wrapper((*(switch_task_namespaces_fn) kallsyms_switch_task_namespaces)(me, nsset.nsproxy));
		nsset.nsproxy = NULL;
	}
out0:
	if (nsset.nsproxy)
		void_ibt_wrapper((*(free_nsproxy_fn) kallsyms_free_nsproxy)(nsset.nsproxy));
out:
	fput(file);
	return err;
#else  /* !defined(STAPCONF_NSSET_COMPLETE) */
	/* NB do nothing; ignore the error when the kernel is too
	 * old to support this. */
	return 0;
#endif
}

/* Returns 0 on success or an error code otherwise. */
static inline int
stap_switch_to_target_mnt_ns_if_needed(bool *switched_ptr)
{
	if (_stp_target_mnt_ns_fd > 0 && has_set_mnt_ns()) {
		bool just_unshared = false;
		int rc;

		if (unlikely(! _stp_fs_struct_unshared)) {
			rc = unshare_fs_struct();
			if (unlikely(rc != 0)) {
				_stp_error("unshare_fs_struct() failed: %d\n",
					   rc);
				return rc;
			}
			_stp_fs_struct_unshared = true;
			just_unshared = true;
		}

		dbug_task(2, "switching mount namespace to the target process's for task '%s'\n",
			  current ? current->comm : "<null>");

		rc = stap_set_mnt_ns(_stp_target_mnt_ns_fd);

		if (unlikely(rc == -EINVAL && ! just_unshared)) {
			/* setns() is per-thread so it's still possible for
			 * a new thread to need a call to unshare_fs_struct() */
			if (unlikely(! _stp_fs_struct_unshared)) {
				rc = unshare_fs_struct();
				if (unlikely(rc != 0)) {
					_stp_error("unshare_fs_struct() failed: %d\n",
						   rc);
					return rc;
				}
				_stp_fs_struct_unshared = true;
			}

			rc = stap_set_mnt_ns(_stp_target_mnt_ns_fd);
		}

		if (unlikely(rc != 0)) {
			_stp_error("setns(%d) failed for the target mount namespace: %d\n",
				   _stp_target_mnt_ns_fd, rc);
			return rc;
		}

		*switched_ptr = true;
	}

	return 0;
}

static inline int
stap_switch_to_orig_mnt_ns_if_needed(void)
{
	if (likely(_stp_orig_mnt_ns_fd > 0 && has_set_mnt_ns())) {
		int rc;

		dbug_task(2, "switching mount namespace to the original one for task '%s'\n",
			  current ? current->comm : "<null>");

		rc = stap_set_mnt_ns(_stp_orig_mnt_ns_fd);
		if (unlikely(rc != 0)) {
			_stp_error("setns(%d) failed for the original mount namespace: %d\n",
				   _stp_target_mnt_ns_fd, rc);
			return rc;
		}
	}

	return 0;
}

static inline char *
stap_real_path(const char *pathname, size_t *len_ptr)
{
	int rc;
	char *ret;
	char *path_buf = NULL;
	char *p;
	struct path path;
	bool mnt_ns_switched = false;

	might_sleep();

	*len_ptr = 0;

	path_buf = _stp_kmalloc(PATH_MAX);
	if (unlikely(path_buf == NULL)) {
		ret = ERR_PTR(-ENOMEM);
		goto out;
	}

	rc = stap_switch_to_target_mnt_ns_if_needed(&mnt_ns_switched);
	if (unlikely(rc != 0)) {
		ret = ERR_PTR(rc);
		goto out_with_buf;
	}

	rc = kern_path(pathname, LOOKUP_FOLLOW, &path);

	if (mnt_ns_switched) {
		int rc;
		rc = stap_switch_to_orig_mnt_ns_if_needed();
		if (unlikely(rc != 0)) {
			ret = ERR_PTR(rc);
			goto out_with_buf;
		}
	}

	if (unlikely(rc != 0)) {
		_stp_error("Couldn't resolve target program file path '%s': %d\n",
			   pathname, rc);
		ret = ERR_PTR(rc);
		goto out_with_buf;
	}

	p = d_path(&path, path_buf, PATH_MAX);
	if (unlikely(p == NULL)) {
		ret = ERR_PTR(-EINVAL);
		goto out_with_path;
	}

	if (unlikely(IS_ERR(p))) {
		_stp_error("d_path() failed for path '%s': %ld\n", pathname,
			   PTR_ERR(p));
		ret = p;
		goto out_with_path;
	}

#if 0
	pr_warn("path buf: '%s' -> '%s'\n", pathname, p);
#endif

	if (strcmp(p, pathname) != 0) {  /* found a symlink */
		char *q;

		size_t len = strlen(p);
		if (unlikely(len == 0)) {
			ret = ERR_PTR(-EINVAL);
			goto out_with_path;
		}

		q = _stp_kmalloc(len + 1);
		if (unlikely(q == NULL)) {
			ret = ERR_PTR(-ENOMEM);
			goto out_with_path;
		}

		memcpy(q, p, len + 1);
		*len_ptr = len;

		ret = q;
		goto out_with_path;
	}

	ret = NULL;

out_with_path:

	path_put(&path);

out_with_buf:

	if (likely(path_buf != NULL))
		_stp_kfree(path_buf);
out:

    return ret;
}

#endif  /* LINUX_STAP_FS_H */
