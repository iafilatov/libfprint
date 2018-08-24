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

      FILE:    CHAINCODE.C
      AUTHOR:  Michael D. Garris
      DATE:    05/11/1999

      Contains routines responsible for generating and manipulating
      chain codes as part of the NIST Latent Fingerprint System (LFS).

***********************************************************************
               ROUTINES:
                        chain_code_loop()
                        is_chain_clockwise()
***********************************************************************/

#include <stdio.h>
#include <lfs.h>

/*************************************************************************
**************************************************************************
#cat: chain_code_loop - Converts a feature's contour points into an
#cat:            8-connected chain code vector.  This encoding represents
#cat:            the direction taken between each adjacent point in the
#cat:            contour.  Chain codes may be used for many purposes, such
#cat:            as computing the perimeter or area of an object, and they
#cat:            may be used in object detection and recognition.

   Input:
      contour_x - x-coord list for feature's contour points
      contour_y - y-coord list for feature's contour points
      ncontour  - number of points in contour
   Output:
      ochain    - resulting vector of chain codes
      onchain   - number of codes in chain
                  (same as number of points in contour)
   Return Code:
      Zero      - chain code successful derived
      Negative  - system error
**************************************************************************/
int chain_code_loop(int **ochain, int *onchain,
               const int *contour_x, const int *contour_y, const int ncontour)
{
   int *chain;
   int i, j, dx, dy;

   /* If we don't have at least 3 points in the contour ... */
   if(ncontour <= 3){
      /* Then we don't have a loop, so set chain length to 0 */
      /* and return without any allocations.                 */
      *onchain = 0;
      return(0);
   }

   /* Allocate chain code vector.  It will be the same length as the */
   /* number of points in the contour.  There will be one chain code */
   /* between each point on the contour including a code between the */
   /* last to the first point on the contour (completing the loop).  */
   chain = (int *)malloc(ncontour * sizeof(int));
   /* If the allocation fails ... */
   if(chain == (int *)NULL){
      fprintf(stderr, "ERROR : chain_code_loop : malloc : chain\n");
      return(-170);
   }

   /* For each neighboring point in the list (with "i" pointing to the */
   /* previous neighbor and "j" pointing to the next neighbor...       */
   for(i = 0, j=1; i < ncontour-1; i++, j++){
      /* Compute delta in X between neighbors. */
      dx = contour_x[j] - contour_x[i];
      /* Compute delta in Y between neighbors. */
      dy = contour_y[j] - contour_y[i];
      /* Derive chain code index from neighbor deltas.                  */
      /* The deltas are on the range [-1..1], so to use them as indices */
      /* into the code list, they must first be incremented by one.     */
      chain[i] = *(g_chaincodes_nbr8+((dy+1)*NBR8_DIM)+dx+1);
   }

   /* Now derive chain code between last and first points in the */
   /* contour list.                                              */
   dx = contour_x[0] - contour_x[i];
   dy = contour_y[0] - contour_y[i];
   chain[i] = *(g_chaincodes_nbr8+((dy+1)*NBR8_DIM)+dx+1);

   /* Store results to the output pointers. */
   *ochain = chain;
   *onchain = ncontour;

   /* Return normally. */
   return(0);
}

/*************************************************************************
**************************************************************************
#cat: is_chain_clockwise - Takes an 8-connected chain code vector and
#cat:            determines if the codes are ordered clockwise or
#cat:            counter-clockwise.
#cat:            The routine also requires a default return value be
#cat:            specified in the case the the routine is not able to
#cat:            definitively determine the chains direction.  This allows
#cat:            the default response to be application-specific.

   Input:
      chain       - chain code vector
      nchain      - number of codes in chain
      default_ret - default return code (used when we can't tell the order)
   Return Code:
      TRUE      - chain determined to be ordered clockwise
      FALSE     - chain determined to be ordered counter-clockwise
      Default   - could not determine the order of the chain
**************************************************************************/
int is_chain_clockwise(const int *chain, const int nchain,
                       const int default_ret)
{
   int i, j, d, sum;

   /* Initialize turn-accumulator to 0. */
   sum = 0;

   /* Foreach neighboring code in chain, compute the difference in  */
   /* direction and accumulate.  Left-hand turns increment, whereas */
   /* right-hand decrement.                                         */
   for(i = 0, j =1; i < nchain-1; i++, j++){
      /* Compute delta in neighbor direction. */
      d = chain[j] - chain[i];
      /* Make the delta the "inner" distance. */
      /* If delta >= 4, for example if chain_i==2 and chain_j==7 (which   */
      /* means the contour went from a step up to step down-to-the-right) */
      /* then 5=(7-2) which is >=4, so -3=(5-8) which means that the      */
      /* change in direction is a righ-hand turn of 3 units).             */
      if(d >= 4)
         d -= 8;
      /* If delta <= -4, for example if chain_i==7 and chain_j==2 (which  */
      /* means the contour went from a step down-to-the-right to step up) */
      /* then -5=(2-7) which is <=-4, so 3=(-5+8) which means that the    */
      /* change in direction is a left-hand turn of 3 units).             */
      else if (d <= -4)
         d += 8;

      /* The delta direction is then accumulated. */
      sum += d;
   }

   /* Now we need to add in the final delta direction between the last */
   /* and first codes in the chain.                                    */
   d = chain[0] - chain[i];
   if(d >= 4)
      d -= 8;
   else if (d <= -4)
      d += 8;
   sum += d;

   /* If the final turn_accumulator == 0, then we CAN'T TELL the       */
   /* direction of the chain code, so return the default return value. */
   if(sum == 0)
      return(default_ret);
   /* Otherwise, if the final turn-accumulator is positive ... */
   else if(sum > 0)
      /* Then we had a greater amount of left-hand turns than right-hand     */
      /* turns, so the chain is in COUNTER-CLOCKWISE order, so return FALSE. */
      return(FALSE);
   /* Otherwise, the final turn-accumulator is negative ... */
   else
      /* So we had a greater amount of right-hand turns than left-hand  */
      /* turns, so the chain is in CLOCKWISE order, so return TRUE.     */
      return(TRUE);
}
