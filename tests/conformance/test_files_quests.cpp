/*
 * test_files_quests.cpp - the real quests through the packfile layer the
 * way the player opens them: decode_file_007 (stdio, or the packfile layer
 * for "qst.dat#NESQST_NEW_QST"), then the LZSS-packed, password-scrambled
 * packfile. Checks the decoded file, the unpacked stream (hash, size, EOF
 * flags), a long mix of pack_fread/pack_fseek/pack_getc/pack_feof over the
 * stream, and the other direction: packing the stream again (as save games
 * are written) must produce the reference's bytes.
 */
#include "conf.h"
#include "conf_zc.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

namespace
{

bool find_quest(conf::Ctx &ctx, const char *where, bool data_dir, std::string *path)
{
    if(strstr(where, ".dat#"))
    {
        std::string dat(where, (size_t)(strchr(where, '#') - where));

        if(!ctx.have_data(dat.c_str()))
            return false;

        *path = ctx.data(dat.c_str()) + strchr(where, '#');
        return true;
    }

    if(data_dir ? !ctx.have_data(where) : !ctx.have_quest(where))
        return false;

    *path = data_dir ? ctx.data(where) : ctx.quest(where);
    return true;
}

void run_quest(conf::Ctx &ctx, const char *where, bool data_dir, const char *label)
{
    std::string path;

    if(!find_quest(ctx, where, data_dir, &path))
    {
        ctx.skip("%s not found", where);
        return;
    }

    std::string dec = ctx.tmp(conf::nm("%s.dec", label));
    int method = -1;
    ctx.check_int("decode", conf::zc::decode_quest(path.c_str(), dec.c_str(), &method));
    ctx.check_int("method", method);
    ctx.check_file("decoded", dec.c_str());

    /* the whole stream */
    PACKFILE *f = conf::zc::open_decoded_quest(dec.c_str());
    ctx.expect("open", f != NULL);

    if(!f)
        return;

    std::vector<unsigned char> all;
    std::vector<unsigned char> buf(4096);
    conf::Hash eofs;
    long got;

    while((got = pack_fread(&buf[0], 4096, f)) > 0)
    {
        all.insert(all.end(), buf.begin(), buf.begin() + got);
        eofs.add_i32((int)got);
        eofs.add_i32(pack_feof(f) ? 1 : 0);

        if(got < 4096)
            break;
    }

    ctx.checkf("unpacked", "%lu bytes %s", (unsigned long)all.size(),
               conf::hash_text(conf::hash_bytes(all.empty() ? NULL : &all[0], all.size())).c_str());
    ctx.check_hash("fread_eof_flags", eofs.value());
    ctx.check_int("getc_after", pack_getc(f));
    ctx.check_int("feof_after", pack_feof(f) ? 1 : 0);
    ctx.check_int("ferror_after", pack_ferror(f) ? 1 : 0);
    pack_fclose(f);

    /* EOF flag around the last bytes, read one at a time */
    f = conf::zc::open_decoded_quest(dec.c_str());
    long skip = (long)all.size() - 40;

    if(skip > 0)
        pack_fseek(f, (int)skip);

    conf::Seq tail(ctx, "tail");

    for(int i = 0; i < 44; ++i)
    {
        tail.add_int(pack_getc(f), "getc %d", i);
        tail.add_int(pack_feof(f) ? 1 : 0, "feof %d", i);
    }

    tail.end();
    pack_fclose(f);

    /* random mix of reads and seeks, like the quest loader's pfread(keep=false) */
    f = conf::zc::open_decoded_quest(dec.c_str());
    conf::Seq mix(ctx, "mix");
    conf::Hash data;

    for(int step = 0; step < 20000 && !pack_feof(f); ++step)
    {
        int n = ctx.rng.range(0, 9) == 0 ? ctx.rng.range(4000, 70000) : ctx.rng.range(1, 600);
        int op = ctx.rng.range(0, 3);

        if(op == 0)
        {
            mix.add_int(pack_fseek(f, n), "seek %d step %d", n, step);
        }
        else if(op == 1)
        {
            std::vector<unsigned char> b((size_t)n);
            long r = pack_fread(&b[0], n, f);
            mix.add_int(r, "fread %d step %d", n, step);
            data.add_bytes(&b[0], (size_t)(r > 0 ? r : 0));
        }
        else if(op == 2)
        {
            mix.add_int(pack_mgetl(f), "mgetl step %d", step);
        }
        else
        {
            mix.add_int(pack_igetw(f), "igetw step %d", step);
        }

        mix.add_int(pack_feof(f) ? 1 : 0, "feof step %d", step);
    }

    mix.add_hex(data.value(), "data");
    mix.end();
    pack_fclose(f);

    /* packed again with the data password and with none (save games) */
    std::string re = ctx.tmp(conf::nm("%s.repacked", label));
    const char *pws[2] = { conf::zc::datapwd(), "" };

    for(int k = 0; k < 2; ++k)
    {
        f = conf::zc::detail::pack_fopen_password(re.c_str(), F_WRITE_PACKED, pws[k]);
        ctx.expect(conf::nm("repack_open_%d", k), f != NULL);

        if(!f)
            continue;

        size_t at = 0;

        while(at < all.size())
        {
            size_t n = all.size() - at;

            if(n > 20000)
                n = 20000;

            pack_fwrite(&all[at], (long)n, f);
            at += n;
        }

        pack_fclose(f);
        ctx.check_file(conf::nm("repacked_%d", k), re.c_str());

        /* and read back */
        f = conf::zc::detail::pack_fopen_password(re.c_str(), F_READ_PACKED, pws[k]);
        conf::Hash h;
        unsigned long n = 0;

        while((got = pack_fread(&buf[0], 4096, f)) > 0)
        {
            h.add_bytes(&buf[0], (size_t)got);
            n += (unsigned long)got;
        }

        pack_fclose(f);
        ctx.checkf(conf::nm("reread_%d", k), "%lu bytes %s", n, conf::hash_text(h.value()).c_str());
    }

    delete_file(re.c_str());
    delete_file(dec.c_str());
}

} /* namespace */

CONF_TEST(quest_1st)
{
    run_quest(ctx, "1st.qst", true, "q1");
}

CONF_TEST(quest_2nd)
{
    run_quest(ctx, "2nd.qst", true, "q2");
}

CONF_TEST(quest_3rd)
{
    run_quest(ctx, "3rd.qst", true, "q3");
}

CONF_TEST(quest_4th)
{
    run_quest(ctx, "4th.qst", true, "q4");
}

CONF_TEST(quest_5th)
{
    run_quest(ctx, "5th.qst", true, "q5");
}

CONF_TEST(quest_qstdat)
{
    run_quest(ctx, "qst.dat#NESQST_NEW_QST", true, "qd");
}

CONF_TEST(quest_revali)
{
    run_quest(ctx, "q839_TheLegendofRevaliHatesEnd/The Legend of Revali.qst", false, "revali");
}

CONF_TEST(quest_dreams)
{
    run_quest(ctx, "q804_DreamsRealm/Dreams_Realm 2.53.qst", false, "dreams");
}
