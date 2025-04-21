/* -*- linux-c -*-
 *
 * /proc transport and control
 * Copyright (C) 2005-2018 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include "relay_compat.h"
#include "proc_fs_compatibility.h"

#if defined(STAPCONF_PATH_LOOKUP) && !defined(STAPCONF_KERN_PATH_PARENT)
#define kern_path_parent(name, nameidata) \
	path_lookup(name, LOOKUP_PARENT, nameidata)
#endif

/* _stp_procfs_module_dir is the '/proc/systemtap/{module_name}' directory. */
static struct proc_dir_entry *_stp_procfs_module_dir = NULL;
static struct path _stp_procfs_module_dir_path;

static bool _stp_proc_dir_exists(void)
{
	int found = 0;
#if defined(STAPCONF_PATH_LOOKUP) || defined(STAPCONF_KERN_PATH_PARENT)
	struct nameidata nd;
#else  /* STAPCONF_VFS_PATH_LOOKUP or STAPCONF_KERN_PATH */
	struct path path;
#if defined(STAPCONF_VFS_PATH_LOOKUP)
	struct vfsmount *mnt;
#endif
	int rc;
#endif	/* STAPCONF_VFS_PATH_LOOKUP or STAPCONF_KERN_PATH */

#if defined(STAPCONF_PATH_LOOKUP) || defined(STAPCONF_KERN_PATH_PARENT)
	/* Why "/proc/systemtap/foo"?  kern_path_parent() is basically
	 * the same thing as calling the old path_lookup() with flags
	 * set to LOOKUP_PARENT, which means to look up the parent of
	 * the path, which in this case is "/proc/systemtap". */
	if (! kern_path_parent("/proc/systemtap/foo", &nd)) {
		found = 1;
#ifdef STAPCONF_NAMEIDATA_CLEANUP
		path_put(&nd.path);
#else  /* !STAPCONF_NAMEIDATA_CLEANUP */
		path_release(&nd);
#endif	/* !STAPCONF_NAMEIDATA_CLEANUP */
	}

#elif defined(STAPCONF_KERN_PATH)
	/* Prefer kern_path() over vfs_path_lookup(), since on some
	 * kernels the declaration for vfs_path_lookup() was moved to
	 * a private header. */

	/* See if '/proc/systemtap' exists. */
	rc = kern_path("/proc/systemtap", 0, &path);
	if (rc == 0) {
		found = 1;
		path_put (&path);
	}

#else  /* STAPCONF_VFS_PATH_LOOKUP */
	/* See if '/proc/systemtap' exists. */
	mnt = init_pid_ns.proc_mnt;
	rc = vfs_path_lookup(mnt->mnt_root, mnt, "systemtap", 0, &path);
	if (rc == 0) {
		found = 1;
		path_put (&path);
	}
#endif	/* STAPCONF_VFS_PATH_LOOKUP */

	return found;
}

/*
 * Safely creates '/proc/systemtap' (if necessary) and
 * '/proc/systemtap/{module_name}'.
 *
 * NB: this function is suitable to call from early in the the
 * module-init function, and doesn't rely on any other facilities
 * in our runtime.  PR19833.  See also PR15408.
 */
static int _stp_mkdir_proc_module(void)
{
	static char proc_root_name[STP_MODULE_NAME_LEN + sizeof("systemtap/")];
	int rc;

        if (_stp_procfs_module_dir != NULL)
		return 0;

	/* If we couldn't find "/proc/systemtap", create it. */
	if (!_stp_proc_dir_exists()) {
		/*
		 * We need some sleepable way to synchronize with other stap
		 * modules which are also being loaded for the first time on
		 * this system. The `/proc/systemtap` directory is never removed
		 * after it's made, so this race only happens briefly the first
		 * time stap is used on the current boot. On 3.19+ kernels, the
		 * race results in only a WARN and proc_mkdir() failing; nothing
		 * more than that. However, on kernels <3.19, proc_mkdir()
		 * doesn't error out when a duplicate directory is made, and
		 * instead there are leaks in addition to the WARN (see kernel
		 * commit b208d54b7539 for details). We'd like to fix the leak
		 * and ideally not scare sysadmins with WARNs, so we abuse
		 * `module_mutex` in the kernel for mutual exclusion between all
		 * stap modules. Since `module_mutex` isn't ours to abuse
		 * freely, we elide it by checking if `/proc/systemtap` exists
		 * first, and if it doesn't, then we check again after taking
		 * the lock. This means we'll only use `module_mutex` and
		 * redundantly check for the existence of `/proc/systemtap` just
		 * once for each of the first stap modules loaded on the system,
		 * and only for those stap modules which encounter the race.
		 * After the race window, we're back to just the single check
		 * for `/proc/systemtap` and nothing more.
		 *
		 * This doesn't work on 5.12+ kernels though, as `module_mutex`
		 * is no longer exported, but that isn't a big deal. Since
		 * there's no risk of a leak on 5.12+ kernels, the worst that
		 * can happen is a cosmetic WARN.
		 *
		 * We never need to check proc_mkdir() for an error because, if
		 * it fails on 5.12+ without `module_mutex` due to the directory
		 * already existing, then it's guaranteed that the directory
		 * will be immediately available to use since procfs serializes
		 * the existence check and the registration under the same hold
		 * of a global lock. And if there's a proc_mkdir() error even
		 * with `module_mutex`, then the other proc_mkdir() attempt
		 * below which *is* checked for errors will fail anyway and
		 * produce a fatal error message.
		 */
#ifdef STAPCONF_MODULE_MUTEX
		mutex_lock(&module_mutex);
		if (!_stp_proc_dir_exists())
			proc_mkdir("systemtap", NULL);
		mutex_unlock(&module_mutex);
#else
		proc_mkdir("systemtap", NULL);
#endif
	}

	/* Create the "systemtap/{module_name} directory in procfs. */
	strlcpy(proc_root_name, "/proc/systemtap/", sizeof(proc_root_name));
	strlcat(proc_root_name, THIS_MODULE->name, sizeof(proc_root_name));
	_stp_procfs_module_dir = proc_mkdir(&proc_root_name[6], NULL); // skip the /proc/
#ifdef STAPCONF_PROCFS_OWNER
	if (_stp_procfs_module_dir != NULL)
		_stp_procfs_module_dir->owner = THIS_MODULE;
#endif
	if (_stp_procfs_module_dir == NULL)
		errk("Unable to create '/proc/systemap/%s':"
		     " proc_mkdir failed.\n", THIS_MODULE->name);
        else {
                rc = kern_path(proc_root_name, 0, &_stp_procfs_module_dir_path);
                if (rc != 0) {
                        errk("Unable to resolve /proc/systemap/%s':"
                             " to path.\n", THIS_MODULE->name);
                        proc_remove(_stp_procfs_module_dir);
                        _stp_procfs_module_dir = NULL;
                        return rc;
                }
        }

done:
	return (_stp_procfs_module_dir) ? 0 : -EINVAL;
}


/*
 * Removes '/proc/systemtap/{module_name}'. Notice we're leaving
 * '/proc/systemtap' behind.  There is no way on newer kernels to know
 * if a procfs directory is empty.
 *
 * NB: this is suitable to call late in the module cleanup function,
 * and does not rely on any other facilities in the runtime.  PR19833.
 * See also PR15408.
 */
static void _stp_rmdir_proc_module(void)
{
	if (_stp_procfs_module_dir) {
                path_put(& _stp_procfs_module_dir_path);
		proc_remove(_stp_procfs_module_dir);
		_stp_procfs_module_dir = NULL;
	}
}


inline static int _stp_procfs_ctl_write_fs(int type, void *data, unsigned len)
{
	struct _stp_buffer *bptr;
	unsigned long flags;

#define WRITE_AGG
#ifdef WRITE_AGG
	stp_spin_lock_irqsave(&_stp_ctl_ready_lock, flags);
	if (!list_empty(&_stp_ctl_ready_q)) {
		bptr = (struct _stp_buffer *)_stp_ctl_ready_q.prev;
		if ((bptr->len + len) <= STP_CTL_BUFFER_SIZE
		    && type == STP_REALTIME_DATA
		    && bptr->type == STP_REALTIME_DATA) {
			memcpy(bptr->buf + bptr->len, data, len);
			bptr->len += len;
			stp_spin_unlock_irqrestore(&_stp_ctl_ready_lock, flags);
			return len;
		}
	}
	stp_spin_unlock_irqrestore(&_stp_ctl_ready_lock, flags);
#endif
	return 0;
}

static int _stp_proc_ctl_read_bufsize(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sprintf(page, "%d,%d\n", _stp_nsubbufs, _stp_subbuf_size);
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}


static struct file_operations _stp_ctl_fops_cmd;
#ifdef STAPCONF_PROC_OPS /* control.c */
static struct proc_ops _stp_ctl_proc_ops_cmd;
#endif


static int _stp_procfs_register_ctl_channel_fs(void)
{
	struct proc_dir_entry *bs = NULL;
	struct proc_dir_entry *de;

	if (_stp_mkdir_proc_module())
		goto err0;

	/* create /proc/systemtap/module_name/.cmd  */
#ifdef STAPCONF_PROC_OPS
	de = proc_create(".cmd", 0600, _stp_procfs_module_dir, &_stp_ctl_proc_ops_cmd);
#else
	de = proc_create(".cmd", 0600, _stp_procfs_module_dir, &_stp_ctl_fops_cmd);        
#endif
	if (de == NULL)
		goto err1;
        proc_set_user(de, KUIDT_INIT(_stp_uid), KGIDT_INIT(_stp_gid));

	return 0;

err1:
	_stp_rmdir_proc_module();
err0:
	return -1;
}

static void _stp_procfs_unregister_ctl_channel_fs(void)
{
	remove_proc_entry(".cmd", _stp_procfs_module_dir);
	_stp_rmdir_proc_module();
}



#ifdef STAPCONF_PROC_OPS
struct proc_ops relay_procfs_operations;
#else
struct file_operations relay_procfs_operations;
#endif


// We need to map procfs concepts of proc_dir_entry* and relayfs/vfs of path/dentry*.
struct procfs_relay_file
{
        struct path p;               // contains the dentry*
        struct proc_dir_entry *pde;  // entry valid if this pointer non-NULL
};
struct procfs_relay_file *p_r_files;


static int _stp_procfs_transport_fs_init(const char *module_name)
{
  p_r_files = _stp_vzalloc(num_possible_cpus()
                           * sizeof(struct procfs_relay_file));
  if (unlikely(p_r_files == NULL))
    return -ENOMEM;

#ifdef STAPCONF_PROC_OPS
  relay_procfs_operations.proc_open = __stp_relay_file_open;
  relay_procfs_operations.proc_poll = __stp_relay_file_poll;
  relay_procfs_operations.proc_mmap = relay_file_operations.mmap;
  relay_procfs_operations.proc_read = __stp_relay_file_read;
  relay_procfs_operations.proc_lseek = relay_file_operations.llseek;
  relay_procfs_operations.proc_release = relay_file_operations.release;
#else
  relay_procfs_operations = relay_file_operations;
  relay_procfs_operations.open = __stp_relay_file_open;
  relay_procfs_operations.owner = THIS_MODULE;
  relay_procfs_operations.poll = __stp_relay_file_poll;
  relay_procfs_operations.read = __stp_relay_file_read;
#endif
  
  if (_stp_mkdir_proc_module()) { // get the _stp_procfs_module_dir* created
          _stp_vfree (p_r_files);
          p_r_files = NULL;
          return -1;
  }

  dbug_trans(1, "transport_fs_init dentry=%08lx pde=%08lx ",
             (unsigned long) _stp_procfs_module_dir_path.dentry,
             (unsigned long) _stp_procfs_module_dir);
  
  if (_stp_transport_data_fs_init() != 0) {
          _stp_rmdir_proc_module();
          _stp_vfree (p_r_files);
          p_r_files = NULL;
          return -1;
  }
  
  return 0;
}


static void _stp_procfs_transport_fs_close(void)
{
	_stp_transport_data_fs_close();

	if (likely(p_r_files)) {
        	_stp_vfree (p_r_files);
        	p_r_files = NULL;
        }
}


static struct dentry *_stp_procfs_get_module_dir(void)
{
        return _stp_procfs_module_dir_path.dentry;
}


static int __stp_procfs_relay_remove_buf_file_callback(struct dentry *dentry)
{
  unsigned i;
  struct proc_dir_entry *pde = NULL;
  
  // find the corresponding pde*

  /* NB We cannot use the for_each_online_cpu() here since online
   * CPUs may get changed on-the-fly through the CPU hotplug feature
   * of the kernel.
   */
  for_each_possible_cpu(i)
    {
      if (p_r_files[i].pde != NULL &&
          p_r_files[i].p.dentry == dentry)
        break;
    }

  if (i != num_possible_cpus())
    {
      pde = p_r_files[i].pde;
      proc_remove (pde);
      path_put (& p_r_files[i].p);
      p_r_files[i].pde = NULL;
    }
  
  dbug_trans(1, "remove-buf dentry=%08lx pde=%08lx i=%u",
             (unsigned long) dentry, (unsigned long) pde, i);
  return 0;
}


static struct dentry *
__stp_procfs_relay_create_buf_file_callback(const char *filename,
                                            struct dentry *parent,
#ifdef STAPCONF_RELAY_UMODE_T
                                            umode_t mode,
#else
                                            int mode,
#endif
                                            struct rchan_buf *buf,
                                            int *is_global)
{
  int rc = 0;
  struct dentry* de = NULL;
  char fullpath[sizeof("/proc/systemtap") + STP_MODULE_NAME_LEN + sizeof("/traceNNNNN") + 42];
  struct proc_dir_entry *pde;
  unsigned i = 0;
  struct inode* in;
  
  if (is_global)
          *is_global = 0;
  
  if (parent != _stp_procfs_module_dir_path.dentry)
    goto out;
  
  pde = proc_create (filename, 0400,
                     _stp_procfs_module_dir,
                     & relay_procfs_operations);
  if (pde == NULL)
    goto out;

  proc_set_user(pde, KUIDT_INIT(_stp_uid), KGIDT_INIT(_stp_gid));
  
  rc = snprintf(fullpath, sizeof(fullpath), "/proc/systemtap/%s/%s",
                THIS_MODULE->name, filename);
  
  // find spot to plop this

  /* NB We cannot use the for_each_online_cpu() here since online
   * CPUs may get changed on-the-fly through the CPU hotplug feature
   * of the kernel.
   */
  for_each_possible_cpu(i)
    {
      if (p_r_files[i].pde == NULL)
        break;
    }
  if (i == num_possible_cpus())
    goto out1;
  
  rc = kern_path (fullpath, 0, &p_r_files[i].p);
  if (rc)
    goto out1;
  p_r_files[i].pde = pde;
  de = p_r_files[i].p.dentry;
  
  // fill in the relayfs i_private
  in = de->d_inode;
  in->i_private = buf;
  
  // success!
  goto out;
  
out1:
  proc_remove (pde);

out:
  dbug_trans(1, "create-buf name=%s parent=%08lx -> i=%u rc=%d de=%08lx",
             filename, (unsigned long) parent,
             i, rc, (unsigned long) de);
  return de;
}
