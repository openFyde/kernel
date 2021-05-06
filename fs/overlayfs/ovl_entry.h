// Copyright 2021 The FydeOS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __OVERLAY_FS_ENTRY__
#define __OVERLAY_FS_ENTRY__
struct ovl_config {
  char *lowerdir;
  char *upperdir;
  char *workdir;
};

/* private information held for overlayfs's superblock */
struct ovl_fs {
  struct vfsmount *upper_mnt;
  unsigned numlower;
  struct vfsmount **lower_mnt;
  struct dentry *workdir;
  long lower_namelen;
  /* pathnames of lower and upper dirs, for show_options */
  struct ovl_config config;
  /* creds of process who forced instantiation of super block */
  const struct cred *creator_cred;
};

struct ovl_dir_cache;

/* private information held for every overlayfs dentry */
struct ovl_entry {
  struct dentry *__upperdentry;
  struct ovl_dir_cache *cache;
  union {
    struct {
      u64 version;
      bool opaque;
    };
    struct rcu_head rcu;
  };
  unsigned numlower;
  struct path lowerstack[];
};

static inline struct dentry *__ovl_dentry_lower(struct ovl_entry *oe)
{
  return oe->numlower ? oe->lowerstack[0].dentry : NULL;
}

static inline struct dentry *ovl_upperdentry_dereference(struct ovl_entry *oe)
{
  return lockless_dereference(oe->__upperdentry);
}

static inline bool is_overlay_sb(struct super_block *sb)
{
  const char* fstype = sb->s_type->name;
  return strcmp(fstype, "overlay") == 0;
}

static inline bool is_overlay_inode(struct inode *inode)
{
  return is_overlay_sb(inode->i_sb);
}
#endif // end of __OVERLAY_FS_ENTRY__
