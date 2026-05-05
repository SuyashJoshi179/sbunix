#ifndef _SYS_MOUNT_H
#define _SYS_MOUNT_H

/* sbunix mount(2): no source/flags/data — fstype alone selects the
 * backing fs ("proc" or "disk"). target is an absolute path that
 * already exists in the namespace. */
int mount(const char *target, const char *fstype);

#endif
