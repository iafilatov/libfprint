/*******************************************************************************

License: 
This software was developed at the National Institute of Standards and 
Technology (NIST) by employees of the Federal Government in the course 
of their official duties. Pursuant to title 17 Section 105 of the 
United States Code, this software is not subject to copyright protection 
and is in the public domain. NIST assumes no responsibility  whatsoever for 
its use by other parties, and makes no guarantees, expressed or implied, 
about its quality, reliability, or any other characteristic. 

Disclaimer: 
This software was developed to promote biometric standards and biometric
technology testing for the Federal Government in accordance with the USA
PATRIOT Act and the Enhanced Border Security and Visa Entry Reform Act.
Specific hardware and software products identified in this software were used
in order to perform the software development.  In no case does such
identification imply recommendation or endorsement by the National Institute
of Standards and Technology, nor does it imply that the products and equipment
identified are necessarily the best available for the purpose.  

*******************************************************************************/

/***********************************************************************
      LIBRARY: LFS - NIST Latent Fingerprint System

      FILE:    XYTREPS.C
      AUTHOR:  Michael D. Garris
      DATE:    09/16/2004

      Contains routines useful in converting minutiae in LFS "native"
      representation into other representations, such as
      M1 (ANSI INCITS 378-2004) & NIST internal representations.

***********************************************************************
               ROUTINES:
                        lfs2nist_minutia_XTY()
                        lfs2m1_minutia_XTY()

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
   Return Code:
      Zero     - successful completion
      Negative - system error
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
   Return Code:
      Zero     - successful completion
      Negative - system error
**************************************************************************/
void lfs2m1_minutia_XYT(int *ox, int *oy, int *ot, const MINUTIA *minutia)
{
   int x, y, t;
   float degrees_per_unit;

   /*       XYT's according to M1 (ANSI INCITS 378-2004):   */ 
   /*       1. pixel coordinates with origin top-left       */
   /*       2. orientation in degrees on range [0..179]     */
   /*          with 0 pointing east and increasing counter  */
   /*          clockwise                                    */
   /*       3. direction pointing up the ridge ending or    */
   /*             bifurcaiton valley                        */

   x = minutia->x;
   y = minutia->y;

   degrees_per_unit = 180 / (float)NUM_DIRECTIONS;
   t = (90 - sround(minutia->direction * degrees_per_unit)) % 360;
   if(t < 0){
      t += 360;
   }

   /* range of theta is 0..179 because angles are in units of 2 degress */
   t = t / 2;

   *ox = x;
   *oy = y;
   *ot = t;
}
