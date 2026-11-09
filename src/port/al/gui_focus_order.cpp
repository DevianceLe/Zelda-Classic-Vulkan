// zcport GUI: the order in which the arrow keys offer the focus.
//
// Written from scratch for zcport (no Allegro code). Measured on the
// reference build (tests/conformance/test_gui_engine_focus.cpp):
//
//   - every other visible, enabled object is a candidate, in dialog order;
//   - its distance from the focused object is the gap between the two boxes
//     along the key's axis plus 8 times the gap across it, where the gap of
//     two pixel ranges is 0 when they overlap and 1 when they touch;
//   - left and right (and up and down) give the same order: the reference
//     does not look at the side the candidate is on;
//   - candidates are sorted with the C library quicksort of the reference
//     (the Microsoft CRT one), so objects at the same distance end up in its
//     characteristic order, which decides between equally near buttons.
//
// The sort below reproduces that quicksort's element moves: partitions of up
// to 8 entries are sorted by repeatedly moving the largest entry (the first
// one found among equals) to the end; larger ones are split around the
// median of their first, middle and last entries, and the smaller part is
// sorted first.

#include "gui_internal.h"

#include <cstdlib>

using namespace zcport_gui;

namespace
{
   struct DistanceKey
   {
      const DIALOG *dialog;
      const DIALOG *from;
      Axis axis;

      static int gap(int a, int alen, int b, int blen)
      {
         int g1 = b - (a + alen - 1);
         int g2 = a - (b + blen - 1);
         int g = (g1 > g2) ? g1 : g2;
         return (g > 0) ? g : 0;
      }

      int distance(int obj) const
      {
         const DIALOG *d = dialog + obj;
         int gx = gap(from->x, from->w, d->x, d->w);
         int gy = gap(from->y, from->h, d->y, d->h);
         return (axis == AXIS_HORIZONTAL) ? gx + gy * 8 : gy + gx * 8;
      }

      // < 0, 0, > 0 like a qsort comparator
      int compare(int a, int b) const
      {
         return distance(a) - distance(b);
      }
   };

   void swap_entries(int *v, int a, int b)
   {
      int t = v[a];
      v[a] = v[b];
      v[b] = t;
   }

   const int SMALL_RANGE = 8;

   // Sorts v[lo..hi] (inclusive) by moving the maximum to the end.
   void sort_small(int *v, int lo, int hi, const DistanceKey &key)
   {
      while (hi > lo) {
         int top = lo;
         for (int i = lo + 1; i <= hi; i++) {
            if (key.compare(v[i], v[top]) > 0)
               top = i;
         }
         swap_entries(v, top, hi);
         hi--;
      }
   }

   void sort_entries(int *v, int n, const DistanceKey &key)
   {
      if (n < 2)
         return;

      // pending ranges; the larger part is pushed, so depth stays log2(n)
      int pending_lo[64], pending_hi[64];
      int pending = 0;
      int lo = 0, hi = n - 1;

      for (;;) {
         int size = hi - lo + 1;

         if (size <= SMALL_RANGE) {
            sort_small(v, lo, hi, key);
         }
         else {
            int mid = lo + size / 2;

            // order the first, middle and last entries
            if (key.compare(v[lo], v[mid]) > 0)
               swap_entries(v, lo, mid);
            if (key.compare(v[lo], v[hi]) > 0)
               swap_entries(v, lo, hi);
            if (key.compare(v[mid], v[hi]) > 0)
               swap_entries(v, mid, hi);

            // partition around v[mid]; the pivot entry moves with swaps
            int left = lo, right = hi;
            for (;;) {
               if (left < mid) {
                  do {
                     left++;
                  } while (left < mid && key.compare(v[left], v[mid]) <= 0);
               }
               if (left >= mid) {
                  do {
                     left++;
                  } while (left <= hi && key.compare(v[left], v[mid]) <= 0);
               }
               do {
                  right--;
               } while (right > mid && key.compare(v[right], v[mid]) > 0);

               if (right < left)
                  break;

               swap_entries(v, left, right);
               if (mid == right)
                  mid = left;
            }

            // skip the entries equal to the pivot just below the split
            right++;
            if (mid < right) {
               do {
                  right--;
               } while (right > mid && key.compare(v[right], v[mid]) == 0);
            }
            if (mid >= right) {
               do {
                  right--;
               } while (right > lo && key.compare(v[right], v[mid]) == 0);
            }

            // now v[lo..right] <= pivot < v[left..hi]
            if (right - lo >= hi - left) {
               if (lo < right && pending < 64) {
                  pending_lo[pending] = lo;
                  pending_hi[pending] = right;
                  pending++;
               }
               if (left < hi) {
                  lo = left;
                  continue;
               }
            }
            else {
               if (left < hi && pending < 64) {
                  pending_lo[pending] = left;
                  pending_hi[pending] = hi;
                  pending++;
               }
               if (lo < right) {
                  hi = right;
                  continue;
               }
            }
         }

         if (pending == 0)
            break;
         pending--;
         lo = pending_lo[pending];
         hi = pending_hi[pending];
      }
   }
}

void zcport_gui::order_by_distance(const DIALOG *dialog, int from, Axis axis, int *order, int n)
{
   DistanceKey key;
   key.dialog = dialog;
   key.from = dialog + from;
   key.axis = axis;
   sort_entries(order, n, key);
}
