#ifndef _TERNFS_NET_COMPAT_H
#define _TERNFS_NET_COMPAT_H
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
  #define COMPAT_SKB_RECV_UDP(sk, flags, err) skb_recv_udp(sk, flags, flags & MSG_DONTWAIT, err)
#else
  #define COMPAT_SKB_RECV_UDP(sk, flags, err) skb_recv_udp(sk, flags, err)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
    #include <linux/net.h>
    #define COMPAT_SET_SOCKOPT(sock, level, op, optval, optlen) kernel_setsockopt(sock, level, op, (char *) optval, optlen)
#else
    #include <linux/sockptr.h>
    #define COMPAT_SET_SOCKOPT(sock, level, op, optval, optlen) sock_setsockopt(sock, level, op, KERNEL_SOCKPTR(optval), optlen)
#endif

#endif /* _TERNFS_NET_COMPAT_H */
