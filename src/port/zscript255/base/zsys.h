// zscript255: what the ZScript compiler uses of upstream 2.55.17's base/zsys.h (the
// rest is Allegro and jwin).
#ifndef _ZSYS_H_
#define _ZSYS_H_

#include "base/zdefs.h"
#include "zsyssimple.h"

#include <string>
#include <functional>

int32_t used_switch(int32_t argc,char *argv[],const char *s);

#endif
