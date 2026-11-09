/*
 * test_files_chunks_portonly.cpp - writing chunks (pack_fopen_chunk on a
 * file opened for writing). The reference build cannot be compared: its
 * chunk writer needs a temporary file and returns NULL in this environment.
 * Checked here: the documented layout (two big-endian sizes; compressed
 * chunks store the LZSS data and the negative data size), reading the
 * chunks back, nesting, and a datafile written with chunks that
 * load_datafile reads. Port only.
 */
#include "conf.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

CONF_TEST(chunk_write_roundtrip)
{
    std::string fn = ctx.tmp("chunkw.bin");
    std::vector<unsigned char> data(5000);

    for(size_t i = 0; i < data.size(); ++i)
        data[i] = (unsigned char)("chunk data "[i % 11] + (i / 700));

    for(int outer = 0; outer < 3; ++outer)
        for(int pack = 0; pack < 2; ++pack)
        {
            const char *modes[] = { F_WRITE, F_WRITE_PACKED, F_WRITE_NOPACK };
            packfile_password(outer == 2 ? "pw" : NULL);
            PACKFILE *f = pack_fopen(fn.c_str(), modes[outer]);
            packfile_password(NULL);
            pack_fwrite("HEAD", 4, f);
            PACKFILE *c = pack_fopen_chunk(f, pack);
            ctx.expect(conf::nm("open_%d_%d", outer, pack), c != NULL);

            if(!c)
            {
                pack_fclose(f);
                continue;
            }

            ctx.expect(conf::nm("write_flag_%d_%d", outer, pack), (c->normal.flags & PACKFILE_FLAG_WRITE) != 0);
            pack_fwrite(&data[0], (long)data.size(), c);
            PACKFILE *n = pack_fopen_chunk(c, 0);
            pack_fputs("nested", n);
            ctx.expect(conf::nm("nested_back_%d_%d", outer, pack), pack_fclose_chunk(n) == c);
            ctx.expect(conf::nm("back_%d_%d", outer, pack), pack_fclose_chunk(c) == f);
            pack_fwrite("TAIL", 4, f);
            pack_fclose(f);

            packfile_password(outer == 2 ? "pw" : NULL);
            f = pack_fopen(fn.c_str(), outer == 0 ? F_READ : F_READ_PACKED);
            packfile_password(NULL);
            char head[5] = { 0 };
            pack_fread(head, 4, f);
            ctx.expect(conf::nm("head_%d_%d", outer, pack), strcmp(head, "HEAD") == 0);

            /* the layout: sizes in front */
            long raw = pack_mgetl(f);
            long sz = pack_mgetl(f);
            long want = (long)data.size() + 8 + 6;   /* data, nested sizes, "nested" */
            ctx.expect(conf::nm("sizes_%d_%d", outer, pack), pack ? (sz == -want && raw > 0 && raw < want) : (raw == want && sz == want),
                       "raw=%ld size=%ld", raw, sz);
            pack_fclose(f);

            packfile_password(outer == 2 ? "pw" : NULL);
            f = pack_fopen(fn.c_str(), outer == 0 ? F_READ : F_READ_PACKED);
            packfile_password(NULL);
            pack_fread(head, 4, f);
            c = pack_fopen_chunk(f, 0);
            std::vector<unsigned char> back(data.size());
            long got = pack_fread(&back[0], (long)back.size(), c);
            ctx.expect(conf::nm("data_%d_%d", outer, pack), got == (long)data.size() && back == data);
            n = pack_fopen_chunk(c, 0);
            char nested[16] = { 0 };
            pack_fread(nested, 15, n);
            ctx.expect(conf::nm("nested_%d_%d", outer, pack), strcmp(nested, "nested") == 0, "%s", nested);
            pack_fclose_chunk(n);
            pack_fclose_chunk(c);
            char tail[5] = { 0 };
            pack_fread(tail, 4, f);
            ctx.expect(conf::nm("tail_%d_%d", outer, pack), strcmp(tail, "TAIL") == 0, "%s", tail);
            pack_fclose(f);
        }

    delete_file(fn.c_str());
}

CONF_TEST(chunk_write_datafile)
{
    std::string fn = ctx.tmp("chunkdat.dat");
    PACKFILE *f = pack_fopen(fn.c_str(), F_WRITE_NOPACK);
    pack_mputl(DAT_MAGIC, f);
    pack_mputl(2, f);

    for(int i = 0; i < 2; ++i)
    {
        const char *name = i ? "SECOND" : "FIRST";
        pack_mputl(DAT_PROPERTY, f);
        pack_mputl(DAT_NAME, f);
        pack_mputl((long)strlen(name), f);
        pack_fwrite(name, (long)strlen(name), f);
        pack_mputl(DAT_DATA, f);
        PACKFILE *c = pack_fopen_chunk(f, i);

        for(int k = 0; k < 3000; ++k)
            pack_putc((k * (i + 3)) & 0xFF, c);

        pack_fclose_chunk(c);
    }

    pack_fclose(f);
    DATAFILE *d = load_datafile(fn.c_str());
    ctx.expect("loaded", d != NULL);

    if(d)
    {
        for(int i = 0; i < 2; ++i)
        {
            bool ok = d[i].type == DAT_DATA && d[i].size == 3000;

            for(int k = 0; ok && k < 3000; ++k)
                ok = ((unsigned char *)d[i].dat)[k] == ((k * (i + 3)) & 0xFF);

            ctx.expect(conf::nm("object_%d", i), ok);
        }

        ctx.expect("names", strcmp(get_datafile_property(&d[1], DAT_NAME), "SECOND") == 0);
        unload_datafile(d);
    }

    delete_file(fn.c_str());
}
