#pragma once

#define major(dev) ((unsigned int)(((dev) >> 8) & 0xff))
#define minor(dev) ((unsigned int)((dev) & 0xff))
#define makedev(ma, mi) ((unsigned int)(((ma) << 8) | (mi)))
