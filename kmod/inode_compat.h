#ifndef _EGGSFS_INODE_COMPAT_H
#define _EGGSFS_INODE_COMPAT_H
#include <linux/version.h>

// Compatibility for user_namespace struct
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
  #define COMPAT_FUNC_UNS(name, ...) name(__VA_ARGS__);
  #define COMPAT_FUNC_UNS_IMP(name, ...) name(__VA_ARGS__)
  #define COMPAT_FUNC_UNS_CALL(name, ...) name(__VA_ARGS__);
#else
  #define COMPAT_FUNC_UNS(name, ...) name(struct user_namespace*, __VA_ARGS__);
  #define COMPAT_FUNC_UNS_IMP(name, ...) name(struct user_namespace* uns, __VA_ARGS__)
  #define COMPAT_FUNC_UNS_CALL(name, ...) name(uns, __VA_ARGS__);
#endif

#endif /* _EGGSFS_INODE_COMPAT_H */
