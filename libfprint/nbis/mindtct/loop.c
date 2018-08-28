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

      FILE:    LOOP.C
      AUTHOR:  Michael D. Garris
      DATE:    05/11/1999
      UPDATED: 10/04/1999 Version 2 by MDG
      UPDATED: 03/16/2005 by MDG

      Contains routines responsible for analyzing and filling
      lakes and islands within a binary image as part of the
      NIST Latent Fingerprint System (LFS).

***********************************************************************
               ROUTINES:
                        get_loop_list()
                        on_loop()
                        on_island_lake()
                        on_hook()
                        is_loop_clockwise()
                        process_loop()
                        process_loop_V2()
                        get_loop_aspect()
                        fill_loop()
                        fill_partial_row()
                        flood_loop()
                        flood_fill4()
***********************************************************************/

#include <stdio.h>
#include <lfs.h>

/*************************************************************************
**************************************************************************
#cat: get_loop_list - Takes a list of minutia points and determines which
#cat:                ones lie on loops around valleys (lakes) of a specified
#cat:                maximum circumference.  The routine returns a list of
#cat:                flags, one for each minutia in the input list, and if
#cat:                the minutia is on a qualifying loop, the corresponding
#cat:                flag is set to TRUE, otherwise it is set to FALSE.
#cat:                If for some reason it was not possible to trace the
#cat:                minutia's contour, then it is removed from the list.
#cat:                This can occur due to edits dynamically taking place
#cat:                in the image by other routines.

   Input:
      minutiae  - list of true and false minutiae
      loop_len  - maximum size of loop searched for
      bdata     - binary image data (0==while & 1==black)
      iw        - width (in pixels) of image
      ih        - height (in pixels) of image
   Output:
      oonloop   - loop flags: TRUE == loop, FALSE == no loop
   Return Code:
      Zero     - successful completion
      Negative - system error
**************************************************************************/

/*************************************************************************
**************************************************************************
#cat: on_loop - Determines if a minutia point lies on a loop (island or lake)
#cat:           of specified maximum circumference.

   Input:
      minutiae      - list of true and false minutiae
      max_loop_len  - maximum size of loop searched for
      bdata         - binary image data (0==while & 1==black)
      iw            - width (in pixels) of image
      ih            - height (in pixels) of image
   Return Code:
      IGNORE     - minutia contour could not be traced
      LOOP_FOUND - minutia determined to lie on qualifying loop
      FALSE      - minutia determined not to lie on qualifying loop
      Negative   - system error
**************************************************************************/
int on_loop(const MINUTIA *minutia, const int max_loop_len,
            unsigned char *bdata, const int iw, const int ih)
{
   int ret;
   int *contour_x, *contour_y, *contour_ex, *contour_ey, ncontour;

   /* Trace the contour of the feature starting at the minutia point  */
   /* and stepping along up to the specified maximum number of steps. */
   ret = trace_contour(&contour_x, &contour_y,
                       &contour_ex, &contour_ey, &ncontour, max_loop_len,
                       minutia->x, minutia->y, minutia->x, minutia->y,
                       minutia->ex, minutia->ey,
                       SCAN_CLOCKWISE, bdata, iw, ih);

   /* If trace was not possible ... */
   if(ret == IGNORE)
      return(ret);

   /* If the trace completed a loop ... */
   if(ret == LOOP_FOUND){
      free_contour(contour_x, contour_y, contour_ex, contour_ey);
      return(LOOP_FOUND);
   }

   /* If the trace successfully followed the minutia's contour, but did */
   /* not complete a loop within the specified number of steps ...      */
   if(ret == 0){
      free_contour(contour_x, contour_y, contour_ex, contour_ey);
      return(FALSE);
   }

   /* Otherwise, the trace had an error in following the contour ... */
   return(ret);
}

/*************************************************************************
**************************************************************************
#cat: on_island_lake - Determines if two minutia points lie on the same loop
#cat:                 (island or lake).  If a loop is detected, the contour
#cat:                 points of the loop are returned.

   Input:
      minutia1      - first minutia point
      minutia2      - second minutia point
      max_half_loop - maximum size of half the loop circumference searched for
      bdata         - binary image data (0==while & 1==black)
      iw            - width (in pixels) of image
      ih            - height (in pixels) of image
   Output:
      ocontour_x  - x-pixel coords of loop contour
      ocontour_y  - y-pixel coords of loop contour
      ocontour_x  - x coord of each contour point's edge pixel
      ocontour_y  - y coord of each contour point's edge pixel
      oncontour   - number of points in the contour.
   Return Code:
      IGNORE     - contour could not be traced
      LOOP_FOUND - minutiae determined to lie on same qualifying loop
      FALSE      - minutiae determined not to lie on same qualifying loop
      Negative   - system error
**************************************************************************/
int on_island_lake(int **ocontour_x, int **ocontour_y,
                   int **ocontour_ex, int **ocontour_ey, int *oncontour,
                   const MINUTIA *minutia1, const MINUTIA *minutia2,
                   const int max_half_loop,
                   unsigned char *bdata, const int iw, const int ih)
{
   int i, l, ret;
   int *contour1_x, *contour1_y, *contour1_ex, *contour1_ey, ncontour1;
   int *contour2_x, *contour2_y, *contour2_ex, *contour2_ey, ncontour2;
   int *loop_x, *loop_y, *loop_ex, *loop_ey, nloop;

   /* Trace the contour of the feature starting at the 1st minutia point  */
   /* and stepping along up to the specified maximum number of steps or   */
   /* until 2nd mintuia point is encountered.                             */
   ret = trace_contour(&contour1_x, &contour1_y,
                       &contour1_ex, &contour1_ey, &ncontour1, max_half_loop,
                       minutia2->x, minutia2->y, minutia1->x, minutia1->y,
                       minutia1->ex, minutia1->ey,
                       SCAN_CLOCKWISE, bdata, iw, ih);

   /* If trace was not possible, return IGNORE. */
   if(ret == IGNORE)
      return(ret);

   /* If the trace encounters 2nd minutia point ... */
   if(ret == LOOP_FOUND){

      /* Now, trace the contour of the feature starting at the 2nd minutia, */
      /* continuing to search for edge neighbors clockwise, and stepping    */
      /* along up to the specified maximum number of steps or until 1st     */
      /*  mintuia point is encountered.                                     */
      ret = trace_contour(&contour2_x, &contour2_y,
                        &contour2_ex, &contour2_ey, &ncontour2, max_half_loop,
                        minutia1->x, minutia1->y, minutia2->x, minutia2->y,
                        minutia2->ex, minutia2->ey,
                        SCAN_CLOCKWISE, bdata, iw, ih);

      /* If trace was not possible, return IGNORE. */
      if(ret == IGNORE){
         free_contour(contour1_x, contour1_y, contour1_ex, contour1_ey);
         return(ret);
      }

      /* If the 2nd trace encounters 1st minutia point ... */
      if(ret == LOOP_FOUND){
         /* Combine the 2 half loop contours into one full loop. */

         /* Compute loop length (including the minutia pair). */
         nloop = ncontour1 + ncontour2 + 2;

         /* Allocate loop contour. */
         if((ret = allocate_contour(&loop_x, &loop_y, &loop_ex, &loop_ey,
                                   nloop))){
            free_contour(contour1_x, contour1_y, contour1_ex, contour1_ey);
            free_contour(contour2_x, contour2_y, contour2_ex, contour2_ey);
            return(ret);
         }

         /* Store 1st minutia. */
         l = 0;
         loop_x[l] = minutia1->x;
         loop_y[l] = minutia1->y;
         loop_ex[l] = minutia1->ex;
         loop_ey[l++] = minutia1->ey;
         /* Store first contour. */
         for(i = 0; i < ncontour1; i++){
            loop_x[l] = contour1_x[i];
            loop_y[l] = contour1_y[i];
            loop_ex[l] = contour1_ex[i];
            loop_ey[l++] = contour1_ey[i];
         }
         /* Store 2nd minutia. */
         loop_x[l] = minutia2->x;
         loop_y[l] = minutia2->y;
         loop_ex[l] = minutia2->ex;
         loop_ey[l++] = minutia2->ey;
         /* Store 2nd contour. */
         for(i = 0; i < ncontour2; i++){
            loop_x[l] = contour2_x[i];
            loop_y[l] = contour2_y[i];
            loop_ex[l] = contour2_ex[i];
            loop_ey[l++] = contour2_ey[i];
         }

         /* Deallocate the half loop contours. */
         free_contour(contour1_x, contour1_y, contour1_ex, contour1_ey);
         free_contour(contour2_x, contour2_y, contour2_ex, contour2_ey);

         /* Assign loop contour to return pointers. */
         *ocontour_x = loop_x;
         *ocontour_y = loop_y;
         *ocontour_ex = loop_ex;
         *ocontour_ey = loop_ey;
         *oncontour = nloop;

         /* Then return that an island/lake WAS found (LOOP_FOUND). */
         return(LOOP_FOUND);
      }

      /* If the trace successfully followed 2nd minutia's contour, but   */
      /* did not encounter 1st minutia point within the specified number */
      /* of steps ...                                                    */
      if(ret == 0){
         /* Deallocate the two contours. */
         free_contour(contour1_x, contour1_y, contour1_ex, contour1_ey);
         free_contour(contour2_x, contour2_y, contour2_ex, contour2_ey);
         /* Then return that an island/lake was NOT found (FALSE). */
         return(FALSE);
      }

      /* Otherwise, the 2nd trace had an error in following the contour ... */
      free_contour(contour1_x, contour1_y, contour1_ex, contour1_ey);
      return(ret);
   }

   /* If the 1st trace successfully followed 1st minutia's contour, but   */
   /* did not encounter the 2nd minutia point within the specified number */
   /* of steps ...                                                        */
   if(ret == 0){
      free_contour(contour1_x, contour1_y, contour1_ex, contour1_ey);
      /* Then return that an island/lake was NOT found (FALSE). */
      return(FALSE);
   }

   /* Otherwise, the 1st trace had an error in following the contour ... */
   return(ret);
}

/*************************************************************************
**************************************************************************
#cat: on_hook - Determines if two minutia points lie on a hook on the side
#cat:           of a ridge or valley.

   Input:
      minutia1      - first minutia point
      minutia2      - second minutia point
      max_hook_len  - maximum length of contour searched along for a hook
      bdata         - binary image data (0==while & 1==black)
      iw            - width (in pixels) of image
      ih            - height (in pixels) of image
   Return Code:
      IGNORE     - contour could not be traced
      HOOK_FOUND - minutiae determined to lie on same qualifying hook
      FALSE      - minutiae determined not to lie on same qualifying hook
      Negative   - system error
**************************************************************************/
int on_hook(const MINUTIA *minutia1, const MINUTIA *minutia2,
            const int max_hook_len,
            unsigned char *bdata, const int iw, const int ih)
{
   int ret;
   int *contour_x, *contour_y, *contour_ex, *contour_ey, ncontour;

   /* NOTE: This routine should only be called when the 2 minutia points */
   /*       are of "opposite" type.                                      */

   /* Trace the contour of the feature starting at the 1st minutia's     */
   /* "edge" point and stepping along up to the specified maximum number */
   /* of steps or until the 2nd minutia point is encountered.            */
   /* First search for edge neighbors clockwise.                         */

   ret = trace_contour(&contour_x, &contour_y,
                       &contour_ex, &contour_ey, &ncontour, max_hook_len,
                       minutia2->x, minutia2->y, minutia1->ex, minutia1->ey,
                       minutia1->x, minutia1->y,
                       SCAN_CLOCKWISE, bdata, iw, ih);

   /* If trace was not possible, return IGNORE. */
   if(ret == IGNORE)
      return(ret);

   /* If the trace encountered the second minutia point ... */
   if(ret == LOOP_FOUND){
      free_contour(contour_x, contour_y, contour_ex, contour_ey);
      return(HOOK_FOUND);
   }

   /* If trace had an error in following the contour ... */
   if(ret != 0)
      return(ret);


   /* Otherwise, the trace successfully followed the contour, but did */
   /* not encounter the 2nd minutia point within the specified number */
   /* of steps.                                                       */

   /* Deallocate previously extracted contour. */
   free_contour(contour_x, contour_y, contour_ex, contour_ey);

   /* Try searching contour from 1st minutia "edge" searching for */
   /* edge neighbors counter-clockwise.                           */
   ret = trace_contour(&contour_x, &contour_y,
                       &contour_ex, &contour_ey, &ncontour, max_hook_len,
                       minutia2->x, minutia2->y, minutia1->ex, minutia1->ey,
                       minutia1->x, minutia1->y,
                       SCAN_COUNTER_CLOCKWISE, bdata, iw, ih);

   /* If trace was not possible, return IGNORE. */
   if(ret == IGNORE)
      return(ret);

   /* If the trace encountered the second minutia point ... */
   if(ret == LOOP_FOUND){
      free_contour(contour_x, contour_y, contour_ex, contour_ey);
      return(HOOK_FOUND);
   }

   /* If the trace successfully followed the 1st minutia's contour, but   */
   /* did not encounter the 2nd minutia point within the specified number */
   /* of steps ...      */
   if(ret == 0){
      free_contour(contour_x, contour_y, contour_ex, contour_ey);
      /* Then return hook NOT found (FALSE). */
      return(FALSE);
   }

   /* Otherwise, the 2nd trace had an error in following the contour ... */
   return(ret);
}

/*************************************************************************
**************************************************************************
#cat: is_loop_clockwise - Takes a feature's contour points and determines if
#cat:            the points are ordered clockwise or counter-clockwise about
#cat:            the feature.  The routine also requires a default return
#cat:            value be specified in the case the the routine is not able
#cat:            to definitively determine the contour's order.  This allows
#cat:            the default response to be application-specific.

   Input:
      contour_x   - x-coord list for feature's contour points
      contour_y   - y-coord list for feature's contour points
      ncontour    - number of points in contour
      default_ret - default return code (used when we can't tell the order)
   Return Code:
      TRUE      - contour determined to be ordered clockwise
      FALSE     - contour determined to be ordered counter-clockwise
      Default   - could not determine the order of the contour
      Negative  - system error
**************************************************************************/
int is_loop_clockwise(const int *contour_x, const int *contour_y,
                         const int ncontour, const int default_ret)
{
   int ret;
   int *chain, nchain;

   /* Derive chain code from contour points. */
   if((ret = chain_code_loop(&chain, &nchain,
                             contour_x, contour_y, ncontour)))
      /* If there is a system error, return the error code. */
      return(ret);

   /* If chain is empty... */
   if(nchain == 0){
      /* There wasn't enough contour points to tell, so return the   */
      /* the default return value.  No chain needs to be deallocated */
      /* in this case.                                               */
      return(default_ret);
   }

   /* If the chain code for contour is clockwise ... pass default return   */
   /* value on to this routine to correctly handle the case where we can't */
   /* tell the direction of the chain code.                                */
   ret = is_chain_clockwise(chain, nchain, default_ret);

   /* Free the chain code and return result. */
   free(chain);
   return(ret);
}

/*************************************************************************
**************************************************************************
#cat: process_loop - Takes a contour list that has been determined to form
#cat:            a complete loop, and processes it. If the loop is sufficiently
#cat:            large and elongated, then two minutia points are calculated
#cat:            along the loop's longest aspect axis.  If it is determined
#cat:            that the loop does not contain minutiae, it is filled in the
#cat:            binary image.

   Input:
      contour_x  - x-coord list for loop's contour points
      contour_y  - y-coord list for loop's contour points
      contour_ex - x-coord list for loop's edge points
      contour_ey - y-coord list for loop's edge points
      ncontour   - number of points in contour
      bdata      - binary image data (0==while & 1==black)
      iw         - width (in pixels) of image
      ih         - height (in pixels) of image
      lfsparms   - parameters and thresholds for controlling LFS
   Output:
      minutiae    - points to a list of detected minutia structures
        OR
      bdata      - binary image data with loop filled
   Return Code:
      Zero      - loop processed successfully
      Negative  - system error
**************************************************************************/

/*************************************************************************
**************************************************************************
#cat: process_loop_V2 - Takes a contour list that has been determined to form
#cat:            a complete loop, and processes it. If the loop is sufficiently
#cat:            large and elongated, then two minutia points are calculated
#cat:            along the loop's longest aspect axis.  If it is determined
#cat:            that the loop does not contain minutiae, it is filled in the
#cat:            binary image.

   Input:
      contour_x  - x-coord list for loop's contour points
      contour_y  - y-coord list for loop's contour points
      contour_ex - x-coord list for loop's edge points
      contour_ey - y-coord list for loop's edge points
      ncontour   - number of points in contour
      bdata      - binary image data (0==while & 1==black)
      iw         - width (in pixels) of image
      ih         - height (in pixels) of image
      plow_flow_map  - pixelized Low Ridge Flow Map
      lfsparms   - parameters and thresholds for controlling LFS
   Output:
      minutiae    - points to a list of detected minutia structures
        OR
      bdata      - binary image data with loop filled
   Return Code:
      Zero      - loop processed successfully
      Negative  - system error
**************************************************************************/
int process_loop_V2(MINUTIAE *minutiae,
             const int *contour_x, const int *contour_y,
             const int *contour_ex, const int *contour_ey, const int ncontour,
             unsigned char *bdata, const int iw, const int ih,
             int *plow_flow_map, const LFSPARMS *lfsparms)
{
   int idir, type, appearing;
   double min_dist, max_dist;
   int min_fr, max_fr, min_to, max_to;
   int mid_x, mid_y, mid_pix;
   int feature_pix;
   int ret;
   MINUTIA *minutia;
   int fmapval;
   double reliability;

   /* If contour is empty, then just return. */
   if(ncontour <= 0)
      return(0);

   /* If loop is large enough ... */
   if(ncontour > lfsparms->min_loop_len){
      /* Get pixel value of feature's interior. */
      feature_pix = *(bdata + (contour_y[0] * iw) + contour_x[0]);

      /* Get the aspect dimensions of the loop in units of */
      /* squared distance.                                 */
      get_loop_aspect(&min_fr, &min_to, &min_dist,
                      &max_fr, &max_to, &max_dist,
                      contour_x, contour_y, ncontour);

      /* If loop passes aspect ratio tests ... loop is sufficiently  */
      /* narrow or elongated ...                                     */
      if((min_dist < lfsparms->min_loop_aspect_dist) ||
         ((max_dist/min_dist) >= lfsparms->min_loop_aspect_ratio)){

         /* Update minutiae list with opposite points of max distance */
         /* on the loop.                                             */

         /* First, check if interior point has proper pixel value. */
         mid_x = (contour_x[max_fr]+contour_x[max_to])>>1;
         mid_y = (contour_y[max_fr]+contour_y[max_to])>>1;
         mid_pix = *(bdata + (mid_y * iw) + mid_x);
         /* If interior point is the same as the feature... */
         if(mid_pix == feature_pix){

            /* 1. Treat maximum distance point as a potential minutia. */

            /* Compute direction from maximum loop point to its */
            /* opposite point.                                  */
            idir = line2direction(contour_x[max_fr], contour_y[max_fr],
                                  contour_x[max_to], contour_y[max_to],
                                  lfsparms->num_directions);
            /* Get type of minutia: BIFURCATION or RIDGE_ENDING. */
            type = minutia_type(feature_pix);
            /* Determine if minutia is appearing or disappearing. */
            if((appearing = is_minutia_appearing(
                           contour_x[max_fr], contour_y[max_fr],
                           contour_ex[max_fr], contour_ey[max_fr])) < 0){
               /* Return system error code. */
               return(appearing);
            }

            /* Is the new point in a LOW RIDGE FLOW block? */
            fmapval = *(plow_flow_map+(contour_y[max_fr]*iw)+
                                          contour_x[max_fr]);

            /* If current minutia is in a LOW RIDGE FLOW block ... */
            if(fmapval)
               reliability = MEDIUM_RELIABILITY;
            else
               /* Otherwise, minutia is in a reliable block. */
               reliability = HIGH_RELIABILITY;

            /* Create new minutia object. */
            if((ret = create_minutia(&minutia,
                                    contour_x[max_fr], contour_y[max_fr],
                                    contour_ex[max_fr], contour_ey[max_fr],
                                    idir, reliability,
                                    type, appearing, LOOP_ID))){
               /* Return system error code. */
               return(ret);
            }
            /* Update the minutiae list with potential new minutia.  */
            /* NOTE: Deliberately using version one of this routine. */
            ret = update_minutiae(minutiae, minutia, bdata, iw, ih, lfsparms);

            /* If minuitia IGNORED and not added to the minutia list ... */
            if(ret == IGNORE)
               /* Deallocate the minutia. */
               free_minutia(minutia);

            /* 2. Treat point opposite of maximum distance point as */
            /*    a potential minutia.                              */

            /* Flip the direction 180 degrees. Make sure new direction */
            /* is on the range [0..(ndirsX2)].                         */
            idir += lfsparms->num_directions;
            idir %= (lfsparms->num_directions<<1);

            /* The type of minutia will stay the same. */

            /* Determine if minutia is appearing or disappearing. */
            if((appearing = is_minutia_appearing(
                           contour_x[max_to], contour_y[max_to],
                           contour_ex[max_to], contour_ey[max_to])) < 0){
               /* Return system error code. */
               return(appearing);
            }

            /* Is the new point in a LOW RIDGE FLOW block? */
            fmapval = *(plow_flow_map+(contour_y[max_to]*iw)+
                                          contour_x[max_to]);

            /* If current minutia is in a LOW RIDGE FLOW block ... */
            if(fmapval)
               reliability = MEDIUM_RELIABILITY;
            else
               /* Otherwise, minutia is in a reliable block. */
               reliability = HIGH_RELIABILITY;

            /* Create new minutia object. */
            if((ret = create_minutia(&minutia,
                                    contour_x[max_to], contour_y[max_to],
                                    contour_ex[max_to], contour_ey[max_to],
                                    idir, reliability,
                                    type, appearing, LOOP_ID))){
               /* Return system error code. */
               return(ret);
            }

            /* Update the minutiae list with potential new minutia. */
            /* NOTE: Deliberately using version one of this routine. */
            ret = update_minutiae(minutiae, minutia, bdata, iw, ih, lfsparms);

            /* If minuitia IGNORED and not added to the minutia list ... */
            if(ret == IGNORE)
               /* Deallocate the minutia. */
               free_minutia(minutia);

            /* Done successfully processing this loop, so return normally. */
            return(0);

         } /* Otherwise, loop interior has problems. */
      } /* Otherwise, loop is not the right shape for minutiae. */
   } /* Otherwise, loop's perimeter is too small for minutiae. */

   /* If we get here, we have a loop that is assumed to not contain */
   /* minutiae, so remove the loop from the image.                   */
   ret = fill_loop(contour_x, contour_y, ncontour, bdata, iw, ih);

   /* Return either an error code from fill_loop or return normally. */
   return(ret);
}

/*************************************************************************
**************************************************************************
#cat: get_loop_aspect - Takes a contour list (determined to form a complete
#cat:            loop) and measures the loop's aspect (the largest and smallest
#cat:            distances across the loop) and returns the points on the
#cat:            loop where these distances occur.

   Input:
      contour_x - x-coord list for loop's contour points
      contour_y - y-coord list for loop's contour points
      ncontour  - number of points in contour
   Output:
      omin_fr   - contour point index where minimum aspect occurs
      omin_to   - opposite contour point index where minimum aspect occurs
      omin_dist - the minimum distance across the loop
      omax_fr   - contour point index where maximum aspect occurs
      omax_to   - contour point index where maximum aspect occurs
      omax_dist - the maximum distance across the loop
**************************************************************************/
void get_loop_aspect(int *omin_fr, int *omin_to, double *omin_dist,
              int *omax_fr, int *omax_to, double *omax_dist,
              const int *contour_x, const int *contour_y, const int ncontour)
{
   int halfway, limit;
   int i, j;
   double dist;
   double min_dist, max_dist;
   int min_i, max_i, min_j, max_j;

   /* Compute half the perimeter of the loop. */
   halfway = ncontour>>1;

   /* Take opposite points on the contour and walk half way    */
   /* around the loop.                                         */
   i = 0;
   j = halfway;
   /* Compute squared distance between opposite points on loop. */
   dist = squared_distance(contour_x[i], contour_y[i],
                           contour_x[j], contour_y[j]);

   /* Initialize running minimum and maximum distances along loop. */
   min_dist = dist;
   min_i = i;
   min_j = j;
   max_dist = dist;
   max_i = i;
   max_j = j;
   /* Bump to next pair of opposite points. */
   i++;
   /* Make sure j wraps around end of list. */
   j++;
   j %= ncontour;

   /* If the loop is of even length, then we only need to walk half */
   /* way around as the other half will be exactly redundant.  If   */
   /* the loop is of odd length, then the second half will not be   */
   /* be exactly redundant and the difference "may" be meaningful.  */
   /* If execution speed is an issue, then probably get away with   */
   /* walking only the fist half of the loop under ALL conditions.  */

   /* If loop has odd length ... */
   if(ncontour % 2)
      /* Walk the loop's entire perimeter. */
      limit = ncontour;
   /* Otherwise the loop has even length ... */
   else
      /* Only walk half the perimeter. */
      limit = halfway;

   /* While we have not reached our perimeter limit ... */
   while(i < limit){
      /* Compute squared distance between opposite points on loop. */
      dist = squared_distance(contour_x[i], contour_y[i],
                              contour_x[j], contour_y[j]);
      /* Check the running minimum and maximum distances. */
      if(dist < min_dist){
         min_dist = dist;
         min_i = i;
         min_j = j;
      }
      if(dist > max_dist){
         max_dist = dist;
         max_i = i;
         max_j = j;
      }
      /* Bump to next pair of opposite points. */
      i++;
      /* Make sure j wraps around end of list. */
      j++;
      j %= ncontour;
   }

   /* Assign minimum and maximum distances to output pointers. */
   *omin_fr = min_i;
   *omin_to = min_j;
   *omin_dist = min_dist;
   *omax_fr = max_i;
   *omax_to = max_j;
   *omax_dist = max_dist;
}

/*************************************************************************
**************************************************************************
#cat: fill_loop - Takes a contour list that has been determined to form
#cat:            a complete loop, and fills the loop accounting for
#cat:            complex/concaved shapes.
#cat:            NOTE, I tried using a flood-fill in place of this routine,
#cat:            but the contour (although 8-connected) is NOT guaranteed to
#cat:            be "complete" surrounded (in an 8-connected sense) by pixels
#cat:            of opposite color.  Therefore, the flood would occasionally
#cat:            escape the loop and corrupt the binary image!

   Input:
      contour_x  - x-coord list for loop's contour points
      contour_y  - y-coord list for loop's contour points
      ncontour   - number of points in contour
      bdata      - binary image data (0==while & 1==black)
      iw         - width (in pixels) of image
      ih         - height (in pixels) of image
   Output:
      bdata      - binary image data with loop filled
   Return Code:
      Zero      - loop filled successfully
      Negative  - system error
**************************************************************************/
int fill_loop(const int *contour_x, const int *contour_y,
                 const int ncontour, unsigned char *bdata,
                 const int iw, const int ih)
{
    SHAPE *shape;
    int ret, i, j, x, nx, y;
    int lastj;
    int next_pix, feature_pix, edge_pix;

    /* Create a shape structure from loop's contour. */
    if((ret = shape_from_contour(&shape, contour_x, contour_y, ncontour)))
       /* If system error, then return error code. */
       return(ret);

    /* Get feature pixel value (the value on the interior of the loop */
    /* to be filled).                                                 */
    feature_pix = *(bdata+(contour_y[0]*iw)+contour_x[0]);
    /* Now get edge pixel value (the value on the exterior of the loop    */
    /* to be used to filled the loop).  We can get this value by flipping */
    /* the feature pixel value.                                           */
    if(feature_pix)
       edge_pix = 0;
    else
       edge_pix = 1;

    /* Foreach row in shape... */
    for(i = 0; i < shape->nrows; i++){
       /* Get y-coord of current row in shape. */
       y = shape->rows[i]->y;

       /* There should always be at least 1 contour points in the row.    */
       /* If there isn't, then something is wrong, so post a warning and  */
       /* just return.  This is mostly for debug purposes.                */
       if(shape->rows[i]->npts < 1){
          /* Deallocate the shape. */
          free_shape(shape);
          fprintf(stderr,
          "WARNING : fill_loop : unexpected shape, preempting loop fill\n");
          /* This is unexpected, but not fatal, so return normally. */
          return(0);
       }

       /* Reset x index on row to the left-most contour point in the row. */
       j = 0;
       /* Get first x-coord corresponding to the first contour point on row. */
       x = shape->rows[i]->xs[j];
       /* Fill the first contour point on the row. */
       *(bdata+(y*iw)+x) = edge_pix;
       /* Set the index of last contour point on row. */
       lastj = shape->rows[i]->npts - 1;
       /* While last contour point on row has not been processed... */
       while(j < lastj){

          /* On each interation, we have filled up to the current   */
          /* contour point on the row pointed to by "j", and now we */
          /* need to determine if we need to skip some edge pixels  */
          /* caused by a concavity in the shape or not.             */

          /* Get the next pixel value on the row just right of the     */
          /* last contour point filled.  We know there are more points */
          /* on the row because we haven't processed the last contour  */
          /* point on the row yet.                                     */
          x++;
          next_pix = *(bdata+(y*iw)+x);

          /* If the next pixel is the same value as loop's edge pixels ... */
          if(next_pix == edge_pix){
             /* Then assume we have found a concavity and skip to next */
             /* contour point on row.                                  */
             j++;
             /* Fill the new contour point because we know it is on the */
             /* feature's contour.                                      */
             x = shape->rows[i]->xs[j];
             *(bdata+(y*iw)+x) = edge_pix;

             /* Now we are ready to loop again. */
          }

          /* Otherwise, fill from current pixel up through the next contour */
          /* point to the right on the row.                                 */
          else{
             /* Bump to the next contour point to the right on row. */
             j++;
             /* Set the destination x-coord to the next contour point   */
             /* to the right on row.  Realize that this could be the    */
             /* same pixel as the current x-coord if contour points are */
             /* adjacent.                                               */
             nx = shape->rows[i]->xs[j];

             /* Fill between current x-coord and next contour point to the */
             /* right on the row (including the new contour point).*/
             fill_partial_row(edge_pix, x, nx, y, bdata, iw, ih);
         }

         /* Once we are here we have filled the row up to (and including) */
         /* the contour point currently pointed to by "j".                */
         /* We are now ready to loop again.                               */

     } /* End WHILE */
   } /* End FOR */

   free_shape(shape);

   /* Return normally. */
   return(0);
}

/*************************************************************************
**************************************************************************
#cat: fill_partial_row - Fills a specified range of contiguous pixels on
#cat:            a specified row of an 8-bit pixel image with a specified
#cat:            pixel value.  NOTE, the pixel coordinates are assumed to
#cat:            be within the image boundaries.

   Input:
      fill_pix - pixel value to fill with (should be on range [0..255]
      frx      - x-pixel coord where fill should begin
      tox      - x-pixel coord where fill should end (inclusive)
      y        - y-pixel coord of current row being filled
      bdata    - 8-bit image data
      iw       - width (in pixels) of image
      ih       - height (in pixels) of image
   Output:
      bdata    - 8-bit image data with partial row filled.
**************************************************************************/
void fill_partial_row(const int fill_pix, const int frx, const int tox,
          const int y, unsigned char *bdata, const int iw, const int ih)
{
   int x;
   unsigned char *bptr;

   /* Set pixel pointer to starting x-coord on current row. */
   bptr = bdata+(y*iw)+frx;

   /* Foreach pixel between starting and ending x-coord on row */
   /* (including the end points) ...                           */
   for(x = frx; x <= tox; x++){
      /* Set current pixel with fill pixel value. */
      *bptr = fill_pix;
      /* Bump to next pixel in the row. */
      bptr++;
   }
}

/*************************************************************************
**************************************************************************
#cat: flood_loop - Fills a given contour (determined to form a complete loop)
#cat:            with a specified pixel value using a recursive flood-fill
#cat:            technique.
#cat:            NOTE, this fill approach will NOT always work with the
#cat:            contours generated in this application because they
#cat:            are NOT guaranteed to be ENTIRELY surrounded by 8-connected
#cat:            pixels not equal to the fill pixel value.  This is unfortunate
#cat:            because the flood-fill is a simple algorithm that will handle
#cat:            complex/concaved shapes.

   Input:
      contour_x  - x-coord list for loop's contour points
      contour_y  - y-coord list for loop's contour points
      ncontour   - number of points in contour
      bdata      - binary image data (0==while & 1==black)
      iw         - width (in pixels) of image
      ih         - height (in pixels) of image
   Output:
      bdata      - binary image data with loop filled
**************************************************************************/

/*************************************************************************
**************************************************************************
#cat: flood_fill4 - Recursively floods a region of an 8-bit pixel image with a
#cat:               specified pixel value given a starting (seed) point.  The
#cat:               recursion is based neighbors being 4-connected.

   Input:
      fill_pix - 8-bit pixel value to be filled with (on range [0..255]
      x        - starting x-pixel coord
      y        - starting y-pixel coord
      bdata    - 8-bit pixel image data
      iw       - width (in pixels) of image
      ih       - height (in pixels) of image
   Output:
      bdata    - 8-bit pixel image data with region filled
**************************************************************************/
void flood_fill4(const int fill_pix, const int x, const int y,
                 unsigned char *bdata, const int iw, const int ih)
{
   unsigned char *pptr;
   int y_north, y_south, x_east, x_west;

   /* Get address of current pixel. */
   pptr =  bdata + (y*iw) + x;
   /* If pixel needs to be filled ... */
   if(*pptr != fill_pix){
      /* Fill the current pixel. */
      *pptr = fill_pix;

      /* Recursively invoke flood on the pixel's 4 neighbors.   */
      /* Test to make sure neighbors are within image boudaries */
      /* before invoking each flood.                            */
      y_north = y-1;
      y_south = y+1;
      x_west = x-1;
      x_east = x+1;

      /* Invoke North */
      if(y_north >= 0)
         flood_fill4(fill_pix, x, y_north, bdata, iw, ih);

      /* Invoke East */
      if(x_east < iw)
         flood_fill4(fill_pix, x_east, y, bdata, iw, ih);

      /* Invoke South */
      if(y_south < ih)
         flood_fill4(fill_pix, x, y_south, bdata, iw, ih);

      /* Invoke West */
      if(x_west >= 0)
         flood_fill4(fill_pix, x_west, y, bdata, iw, ih);
   }

   /* Otherwise, there is nothing to be done. */
}
