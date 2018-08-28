/*******************************************************************************

License:
This software and/or related materials was developed at the National Institute
of Standards and Technology (NIST) by employees of the Federal Government
in the course of their official duties. Pursuant to title 17 Section 105
of the United States Code, this software is not subject to copyright
protection and is in the public domain.

This software and/or related materials have been determined to be not subject
to the EAR (see Part 734.3 of the EAR for exact details) because it is
a publicly available technology and software, and is freely distributed
to any interested party with no licensing requirements.  Therefore, it is
permissible to distribute this software as a free download from the internet.

Disclaimer:
This software and/or related materials was developed to promote biometric
standards and biometric technology testing for the Federal Government
in accordance with the USA PATRIOT Act and the Enhanced Border Security
and Visa Entry Reform Act. Specific hardware and software products identified
in this software were used in order to perform the software development.
In no case does such identification imply recommendation or endorsement
by the National Institute of Standards and Technology, nor does it imply that
the products and equipment identified are necessarily the best available
for the purpose.

This software and/or related materials are provided "AS-IS" without warranty
of any kind including NO WARRANTY OF PERFORMANCE, MERCHANTABILITY,
NO WARRANTY OF NON-INFRINGEMENT OF ANY 3RD PARTY INTELLECTUAL PROPERTY
or FITNESS FOR A PARTICULAR PURPOSE or for any purpose whatsoever, for the
licensed product, however used. In no event shall NIST be liable for any
damages and/or costs, including but not limited to incidental or consequential
damages of any kind, including economic damage or injury to property and lost
profits, regardless of whether NIST shall be advised, have reason to know,
or in fact shall know of the possibility.

By using this software, you agree to bear all risk relating to quality,
use and performance of the software and/or related materials.  You agree
to hold the Government harmless from any claim arising from your use
of the software.

*******************************************************************************/


/***********************************************************************
      LIBRARY: LFS - NIST Latent Fingerprint System

      FILE:    XYTREPS.C
      AUTHOR:  Michael D. Garris
      DATE:    09/16/2004
      UPDATED: 01/11/2012

      Contains routines useful in converting minutiae in LFS "native"
      representation into other representations, such as
      M1 (ANSI INCITS 378-2004) & NIST internal representations.

***********************************************************************
               ROUTINES:
                        lfs2nist_minutia_XTY()
                        lfs2m1_minutia_XTY()
                        lfs2nist_format()

***********************************************************************/

#include <lfs.h>
#include <defs.h>

/*************************************************************************
**************************************************************************
#cat: lfs2nist_minutia_XYT - Converts XYT minutiae attributes in LFS native
#cat:        representation to NIST internal representation

   Input:
      minutia  - LFS minutia structure containing attributes to be converted
   Output:
      ox       - NIST internal based x-pixel coordinate
      oy       - NIST internal based y-pixel coordinate
      ot       - NIST internal based minutia direction/orientation
**************************************************************************/
void lfs2nist_minutia_XYT(int *ox, int *oy, int *ot,
                          const MINUTIA *minutia, const int iw, const int ih)
{
   int x, y, t;
   float degrees_per_unit;

   /*       XYT's according to NIST internal rep:           */
    /*      1. pixel coordinates with origin bottom-left    */
   /*       2. orientation in degrees on range [0..360]     */
   /*          with 0 pointing east and increasing counter  */
   /*          clockwise (same as M1)                       */
   /*       3. direction pointing out and away from the     */
   /*             ridge ending or bifurcation valley        */
   /*             (opposite direction from M1)              */

   x = minutia->x;
   y = ih - minutia->y;

   degrees_per_unit = 180 / (float)NUM_DIRECTIONS;

   t = (270 - sround(minutia->direction * degrees_per_unit)) % 360;
   if(t < 0){
      t += 360;
   }

   *ox = x;
   *oy = y;
   *ot = t;
}

/*************************************************************************
**************************************************************************
#cat: lfs2m1_minutia_XYT - Converts XYT minutiae attributes in LFS native
#cat:        representation to M1 (ANSI INCITS 378-2004) representation

   Input:
      minutia  - LFS minutia structure containing attributes to be converted
   Output:
      ox       - M1 based x-pixel coordinate
      oy       - M1 based y-pixel coordinate
      ot       - M1 based minutia direction/orientation
**************************************************************************/

/*************************************************************************
**************************************************************************
#cat:   lfs2nist_format - Takes a minutiae data structure and converts
#cat:                     the XYT minutiae attributes in LFS native
#cat:                     representation to NIST internal representation
   Input:
      iminutiae - minutiae data structure
      iw        - width (in pixels) of the grayscale image
      ih        - height (in pixels) of the grayscale image
   Output:
      iminutiae - overwrite each minutia element in the minutiae data
                  sturcture convernt to nist internal minutiae format
**************************************************************************/

