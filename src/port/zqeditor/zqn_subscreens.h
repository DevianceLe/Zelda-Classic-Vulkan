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

#endif
