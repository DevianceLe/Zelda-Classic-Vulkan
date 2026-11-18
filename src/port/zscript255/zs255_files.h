// zscript255: opening ZScript source files the way upstream's Windows builds see them.
//
// 2.55.17's compiler opens the buffer and every imported file with fopen(name, "r"). On
// Windows that is text mode on a case-insensitive file system: "Ghost.zh" finds ghost.zh,
// CR LF line ends read as LF, and a Ctrl-Z byte ends the file. Quests are written and
// compiled that way, so on other systems zscript255 reads sources the same way, and the
// same buffer gives the same scripts and the same messages everywhere. File names in
// messages stay as the script wrote them, as on Windows. On Windows these are fopen.
#ifndef ZS255_FILES_H
#define ZS255_FILES_H

#include <cstdio>

// fopen(path, "r") as on Windows: the file found without regard to letter case (each
// part of the path), its text with CR LF as LF and cut at the first Ctrl-Z. Returns
// nullptr if there is no such file.
FILE* zs255_fopen_source(char const* path);

// Whether fopen(path, "r") would open a file there on Windows (a folder doesn't count).
bool zs255_source_exists(char const* path);

#endif
