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

      FILE:    DETECT.C
      AUTHOR:  Michael D. Garris
      DATE:    08/16/1999
      UPDATED: 10/04/1999 Version 2 by MDG
      UPDATED: 03/16/2005 by MDG

      Takes an 8-bit grayscale fingerpinrt image and detects minutiae
      as part of the NIST Latent Fingerprint System (LFS).

***********************************************************************
               ROUTINES:
                        lfs_detect_minutiae()
                        lfs_detect_minutiae_V2()

***********************************************************************/

#include <stdio.h>
#include <lfs.h>
#include <mytime.h>
#include <log.h>

/*************************************************************************
#cat: lfs_detect_minutiae - Takes a grayscale fingerprint image (of arbitrary
#cat:          size), and returns a map of directional ridge flow in the image
#cat:          (2 versions), a binarized image designating ridges from valleys,
#cat:          and a list of minutiae (including position, type, direction,
#cat:          neighbors, and ridge counts to neighbors).

   Input:
      idata     - input 8-bit grayscale fingerprint image data
      iw        - width (in pixels) of the image
      ih        - height (in pixels) of the image
      lfsparms  - parameters and thresholds for controlling LFS
   Output:
      ominutiae - resulting list of minutiae
      oimap     - resulting IMAP
                  {invalid (-1) or valid ridge directions}
      onmap     - resulting NMAP
                  {invalid (-1), high-curvature (-2), blanked blocks {-3} or
                   valid ridge directions}
      omw       - width (in blocks) of image maps
      omh       - height (in blocks) of image maps
      obdata    - resulting binarized image
                  {0 = black pixel (ridge) and 255 = white pixel (valley)}
      obw       - width (in pixels) of the binary image
      obh       - height (in pixels) of the binary image
   Return Code:
      Zero      - successful completion
      Negative  - system error
**************************************************************************/

/*************************************************************************
#cat: lfs_detect_minutiae_V2 - Takes a grayscale fingerprint image (of
#cat:          arbitrary size), and returns a set of image block maps,
#cat:          a binarized image designating ridges from valleys,
#cat:          and a list of minutiae (including position, reliability,
#cat:          type, direction, neighbors, and ridge counts to neighbors).
#cat:          The image maps include a ridge flow directional map,
#cat:          a map of low contrast blocks, a map of low ridge flow blocks.
#cat:          and a map of high-curvature blocks.

   Input:
      idata     - input 8-bit grayscale fingerprint image data
      iw        - width (in pixels) of the image
      ih        - height (in pixels) of the image
      lfsparms  - parameters and thresholds for controlling LFS

   Output:
      ominutiae - resulting list of minutiae
      odmap     - resulting Direction Map
                  {invalid (-1) or valid ridge directions}
      olcmap    - resulting Low Contrast Map
                  {low contrast (TRUE), high contrast (FALSE)}
      olfmap    - resulting Low Ridge Flow Map
                  {low ridge flow (TRUE), high ridge flow (FALSE)}
      ohcmap    - resulting High Curvature Map
                  {high curvature (TRUE), low curvature (FALSE)}
      omw       - width (in blocks) of image maps
      omh       - height (in blocks) of image maps
      obdata    - resulting binarized image
                  {0 = black pixel (ridge) and 255 = white pixel (valley)}
      obw       - width (in pixels) of the binary image
      obh       - height (in pixels) of the binary image
   Return Code:
      Zero      - successful completion
      Negative  - system error
**************************************************************************/
int lfs_detect_minutiae_V2(MINUTIAE **ominutiae,
                        int **odmap, int **olcmap, int **olfmap, int **ohcmap,
                        int *omw, int *omh,
                        unsigned char **obdata, int *obw, int *obh,
                        unsigned char *idata, const int iw, const int ih,
                        const LFSPARMS *lfsparms)
{
   unsigned char *pdata, *bdata;
   int pw, ph, bw, bh;
   DIR2RAD *dir2rad;
   DFTWAVES *dftwaves;
   ROTGRIDS *dftgrids;
   ROTGRIDS *dirbingrids;
   int *direction_map, *low_contrast_map, *low_flow_map, *high_curve_map;
   int mw, mh;
   int ret, maxpad;
   MINUTIAE *minutiae;

   set_timer(total_timer);

   /******************/
   /* INITIALIZATION */
   /******************/

   /* If LOG_REPORT defined, open log report file. */
   if((ret = open_logfile()))
      /* If system error, exit with error code. */
      return(ret);

   /* Determine the maximum amount of image padding required to support */
   /* LFS processes.                                                    */
   maxpad = get_max_padding_V2(lfsparms->windowsize, lfsparms->windowoffset,
                          lfsparms->dirbin_grid_w, lfsparms->dirbin_grid_h);

   /* Initialize lookup table for converting integer directions */
   /* to angles in radians.                                     */
   if((ret = init_dir2rad(&dir2rad, lfsparms->num_directions))){
      /* Free memory allocated to this point. */
      return(ret);
   }

   /* Initialize wave form lookup tables for DFT analyses. */
   /* used for direction binarization.                             */
   if((ret = init_dftwaves(&dftwaves, g_dft_coefs, lfsparms->num_dft_waves,
                        lfsparms->windowsize))){
      /* Free memory allocated to this point. */
      free_dir2rad(dir2rad);
      return(ret);
   }

   /* Initialize lookup table for pixel offsets to rotated grids */
   /* used for DFT analyses.                                     */
   if((ret = init_rotgrids(&dftgrids, iw, ih, maxpad,
                        lfsparms->start_dir_angle, lfsparms->num_directions,
                        lfsparms->windowsize, lfsparms->windowsize,
                        RELATIVE2ORIGIN))){
      /* Free memory allocated to this point. */
      free_dir2rad(dir2rad);
      free_dftwaves(dftwaves);
      return(ret);
   }

   /* Pad input image based on max padding. */
   if(maxpad > 0){   /* May not need to pad at all */
      if((ret = pad_uchar_image(&pdata, &pw, &ph, idata, iw, ih,
                             maxpad, lfsparms->pad_value))){
         /* Free memory allocated to this point. */
         free_dir2rad(dir2rad);
         free_dftwaves(dftwaves);
         free_rotgrids(dftgrids);
         return(ret);
      }
   }
   else{
      /* If padding is unnecessary, then copy the input image. */
      pdata = (unsigned char *)malloc(iw*ih);
      if(pdata == (unsigned char *)NULL){
         /* Free memory allocated to this point. */
         free_dir2rad(dir2rad);
         free_dftwaves(dftwaves);
         free_rotgrids(dftgrids);
         fprintf(stderr, "ERROR : lfs_detect_minutiae_V2 : malloc : pdata\n");
         return(-580);
      }
      memcpy(pdata, idata, iw*ih);
      pw = iw;
      ph = ih;
   }

   /* Scale input image to 6 bits [0..63] */
   /* !!! Would like to remove this dependency eventualy !!!     */
   /* But, the DFT computations will need to be changed, and     */
   /* could not get this work upon first attempt. Also, if not   */
   /* careful, I think accumulated power magnitudes may overflow */
   /* doubles.                                                   */
   bits_8to6(pdata, pw, ph);

   print2log("\nINITIALIZATION AND PADDING DONE\n");

   /******************/
   /*      MAPS      */
   /******************/
   set_timer(imap_timer);

   /* Generate block maps from the input image. */
   if((ret = gen_image_maps(&direction_map, &low_contrast_map,
                    &low_flow_map, &high_curve_map, &mw, &mh,
                    pdata, pw, ph, dir2rad, dftwaves, dftgrids, lfsparms))){
      /* Free memory allocated to this point. */
      free_dir2rad(dir2rad);
      free_dftwaves(dftwaves);
      free_rotgrids(dftgrids);
      free(pdata);
      return(ret);
   }
   /* Deallocate working memories. */
   free_dir2rad(dir2rad);
   free_dftwaves(dftwaves);
   free_rotgrids(dftgrids);

   print2log("\nMAPS DONE\n");

   time_accum(imap_timer, imap_time);

   /******************/
   /* BINARIZARION   */
   /******************/
   set_timer(bin_timer);

   /* Initialize lookup table for pixel offsets to rotated grids */
   /* used for directional binarization.                         */
   if((ret = init_rotgrids(&dirbingrids, iw, ih, maxpad,
                        lfsparms->start_dir_angle, lfsparms->num_directions,
                        lfsparms->dirbin_grid_w, lfsparms->dirbin_grid_h,
                        RELATIVE2CENTER))){
      /* Free memory allocated to this point. */
      free(pdata);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      return(ret);
   }

   /* Binarize input image based on NMAP information. */
   if((ret = binarize_V2(&bdata, &bw, &bh,
                      pdata, pw, ph, direction_map, mw, mh,
                      dirbingrids, lfsparms))){
      /* Free memory allocated to this point. */
      free(pdata);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      free_rotgrids(dirbingrids);
      return(ret);
   }

   /* Deallocate working memory. */
   free_rotgrids(dirbingrids);

   /* Check dimension of binary image.  If they are different from */
   /* the input image, then ERROR.                                 */
   if((iw != bw) || (ih != bh)){
      /* Free memory allocated to this point. */
      free(pdata);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      free(bdata);
      fprintf(stderr, "ERROR : lfs_detect_minutiae_V2 :");
      fprintf(stderr,"binary image has bad dimensions : %d, %d\n",
              bw, bh);
      return(-581);
   }

   print2log("\nBINARIZATION DONE\n");

   time_accum(bin_timer, bin_time);

   /******************/
   /*   DETECTION    */
   /******************/
   set_timer(minutia_timer);

   /* Convert 8-bit grayscale binary image [0,255] to */
   /* 8-bit binary image [0,1].                       */
   gray2bin(1, 1, 0, bdata, iw, ih);

   /* Allocate initial list of minutia pointers. */
   if((ret = alloc_minutiae(&minutiae, MAX_MINUTIAE))){
      return(ret);
   }

   /* Detect the minutiae in the binarized image. */
   if((ret = detect_minutiae_V2(minutiae, bdata, iw, ih,
                             direction_map, low_flow_map, high_curve_map,
                             mw, mh, lfsparms))){
      /* Free memory allocated to this point. */
      free(pdata);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      free(bdata);
      return(ret);
   }

   time_accum(minutia_timer, minutia_time);

   set_timer(rm_minutia_timer);

   if((ret = remove_false_minutia_V2(minutiae, bdata, iw, ih,
                       direction_map, low_flow_map, high_curve_map, mw, mh,
                       lfsparms))){
      /* Free memory allocated to this point. */
      free(pdata);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      free(bdata);
      free_minutiae(minutiae);
      return(ret);
   }

   print2log("\nMINUTIA DETECTION DONE\n");

   time_accum(rm_minutia_timer, rm_minutia_time);

   /******************/
   /*  RIDGE COUNTS  */
   /******************/
   set_timer(ridge_count_timer);

   if((ret = count_minutiae_ridges(minutiae, bdata, iw, ih, lfsparms))){
      /* Free memory allocated to this point. */
      free(pdata);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      free_minutiae(minutiae);
      return(ret);
   }


   print2log("\nNEIGHBOR RIDGE COUNT DONE\n");

   time_accum(ridge_count_timer, ridge_count_time);

   /******************/
   /*    WRAP-UP     */
   /******************/

   /* Convert 8-bit binary image [0,1] to 8-bit */
   /* grayscale binary image [0,255].           */
   gray2bin(1, 255, 0, bdata, iw, ih);

   /* Deallocate working memory. */
   free(pdata);

   /* Assign results to output pointers. */
   *odmap = direction_map;
   *olcmap = low_contrast_map;
   *olfmap = low_flow_map;
   *ohcmap = high_curve_map;
   *omw = mw;
   *omh = mh;
   *obdata = bdata;
   *obw = bw;
   *obh = bh;
   *ominutiae = minutiae;

   time_accum(total_timer, total_time);

   /******************/
   /* PRINT TIMINGS  */
   /******************/
   /* These Timings will print when TIMER is defined. */
   /* print MAP generation timing statistics */
   print_time(stderr, "TIMER: MAPS time   = %f (secs)\n", imap_time);
   /* print binarization timing statistics */
   print_time(stderr, "TIMER: Binarization time   = %f (secs)\n", bin_time);
   /* print minutia detection timing statistics */
   print_time(stderr, "TIMER: Minutia Detection time   = %f (secs)\n",
              minutia_time);
   /* print minutia removal timing statistics */
   print_time(stderr, "TIMER: Minutia Removal time   = %f (secs)\n",
              rm_minutia_time);
   /* print neighbor ridge count timing statistics */
   print_time(stderr, "TIMER: Neighbor Ridge Counting time   = %f (secs)\n",
              ridge_count_time);
   /* print total timing statistics */
   print_time(stderr, "TIMER: Total time   = %f (secs)\n", total_time);

   /* If LOG_REPORT defined, close log report file. */
   if((ret = close_logfile()))
      return(ret);

   return(0);
}

