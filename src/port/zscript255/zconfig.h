// zscript255: what the ZScript compiler uses of upstream 2.55.17's zconfig.h. The
// compiler reads its settings with parser/config.cpp (zscript.cfg, [Compiler]); this
// flag tells whether the last lookup fell back to its default.
#ifndef ZC_ZCONFIG_H
#define ZC_ZCONFIG_H

extern bool zc_cfg_defaulted;

// Allegro 4 writes its config file back here in upstream; zscript255 has no Allegro
// config, so there is nothing to write.
inline void flush_config_file() {}

#endif
