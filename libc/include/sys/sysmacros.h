#ifndef _SYS_SYSMACROS_H
#define _SYS_SYSMACROS_H

#define major(dev) ((unsigned int)(((unsigned long long)(dev) >> 8) & 0xfff))
#define minor(dev) ((unsigned int)(((unsigned long long)(dev)) & 0xff))
#define makedev(maj, min) \
    (((unsigned long long)((maj) & 0xfff) << 8) | ((min) & 0xff))

#endif
