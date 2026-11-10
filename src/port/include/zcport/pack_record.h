/* zcport extension: recording what is read from a PACKFILE.
 *
 * While set, every byte read from f (pack_getc, pack_fread and everything built on them)
 * is handed to fn, and pack_fseek forward is read instead of skipped so those bytes come
 * too. pack_ungetc calls fn with unget = 1 to take the last byte back. Only one file is
 * recorded at a time; fn NULL stops. The player never sets it: ZQuest uses it to keep the
 * original bytes of each section of the quest it opens (src/port/zqeditor/zq_sections.cpp).
 */

#ifndef ZCPORT_PACK_RECORD_H
#define ZCPORT_PACK_RECORD_H

#ifdef __cplusplus
extern "C" {
#endif

struct PACKFILE;

typedef void (*zcport_pack_record_fn)(void *user, const unsigned char *bytes, long n, int unget);

void zcport_pack_record(struct PACKFILE *f, zcport_pack_record_fn fn, void *user);

#ifdef __cplusplus
}
#endif

#endif
