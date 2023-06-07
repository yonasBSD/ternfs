#ifndef _EGGSFS_ERR_H
#define _EGGSFS_ERR_H

#include <linux/errno.h>

#include "bincode.h"

bool eggsfs_unexpected_error(int err);
int eggsfs_error_to_linux(int err);

#endif

