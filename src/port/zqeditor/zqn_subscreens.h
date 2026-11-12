// What the newer-version subscreen writer (zqn_subscreens.cpp) knows that the DMap writer needs.

#ifndef ZQN_SUBSCREENS_H
#define ZQN_SUBSCREENS_H

#include <vector>

// When 2.55.17 and 3.0 open a quest with old custom subscreens, a DMap whose active or passive
// subscreen number the old engine couldn't reach (it counted only up to the first empty
// subscreen) gets the number of the subscreen the old engine fell back to, copied into the
// other list if it was of the other kind (fixup_old_dmap_subscreen_indices). The subscreen
// writer writes those copies; this gives the DMap writer every DMap's numbers as upstream then
// holds them, one per DMap (MAXDMAPS). False when the load leaves every DMap's numbers alone.
bool zqn_dmap_subscreens(std::vector<int> &active, std::vector<int> &passive);

// For quests made in 2.55 (zqn_255_subscreens.cpp), which keep the old subscreen format in SUBS
// versions 6 and 7: the section's data (what follows its size) as 3.0 reads it
// (read_old_subscreens, ZCSubscreen::load_old) and then fixes the DMaps' numbers up
// (fixup_old_dmap_subscreen_indices). dm_active and dm_passive: every DMap's numbers (MAXDMAPS)
// as 3.0 holds them at the end of the load, changed by the fixup. active and passive: each
// list's subscreens as 3.0 writes them (ZCSubscreen::write, SUBS version 21). False when the data
// doesn't read the way 3.0 reads it.
bool zqn_old_subscreens_30(const std::vector<unsigned char> &data, int version, std::vector<int> &dm_active,
                           std::vector<int> &dm_passive, std::vector<std::vector<unsigned char> > &active,
                           std::vector<std::vector<unsigned char> > &passive);

// setupsubscreens for 3.0, with the init data's subscreen style (mode): the two default active
// and passive subscreens as 3.0 writes them.
void zqn_default_subscreens_30(int mode, std::vector<std::vector<unsigned char> > &active,
                               std::vector<std::vector<unsigned char> > &passive);

#endif
