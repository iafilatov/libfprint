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

      FILE:    GETMIN.C
      AUTHOR:  Michael D. Garris
      DATE:    09/10/2004
      UPDATED: 03/16/2005 by MDG

      Takes an 8-bit grayscale fingerpinrt image and detects minutiae
      as part of the NIST Latent Fingerprint System (LFS), returning
      minutiae with final reliabilities and maps including a merged
      quality map.

***********************************************************************
               ROUTINES:
                        get_minutiae()

***********************************************************************/

#include <stdio.h>
#include <lfs.h>

/*************************************************************************
**************************************************************************
#cat:   get_minutiae - Takes a grayscale fingerprint image, binarizes the input
#cat:                image, and detects minutiae points using LFS Version 2.
#cat:                The routine passes back the detected minutiae, the
#cat:                binarized image, and a set of image quality maps.

   Input:
      idata    - grayscale fingerprint image data
      iw       - width (in pixels) of the grayscale image
      ih       - height (in pixels) of the grayscale image
      id       - pixel depth (in bits) of the grayscale image
      ppmm     - the scan resolution (in pixels/mm) of the grayscale image
      lfsparms - parameters and thresholds for controlling LFS
   Output:
      ominutiae         - points to a structure containing the
                          detected minutiae
      oquality_map      - resulting integrated image quality map
      odirection_map    - resulting direction map
      olow_contrast_map - resulting low contrast map
      olow_flow_map     - resulting low ridge flow map
      ohigh_curve_map   - resulting high curvature map
      omap_w   - width (in blocks) of image maps
      omap_h   - height (in blocks) of image maps
      obdata   - points to binarized image data
      obw      - width (in pixels) of binarized image
      obh      - height (in pixels) of binarized image
      obd      - pixel depth (in bits) of binarized image
   Return Code:
      Zero     - successful completion
      Negative - system error
**************************************************************************/
int get_minutiae(MINUTIAE **ominutiae, int **oquality_map,
                 int **odirection_map, int **olow_contrast_map,
                 int **olow_flow_map, int **ohigh_curve_map,
                 int *omap_w, int *omap_h,
                 unsigned char **obdata, int *obw, int *obh, int *obd,
                 unsigned char *idata, const int iw, const int ih,
                 const int id, const double ppmm, const LFSPARMS *lfsparms)
{
   int ret;
   MINUTIAE *minutiae;
   int *direction_map, *low_contrast_map, *low_flow_map;
   int *high_curve_map, *quality_map;
   int map_w, map_h;
   unsigned char *bdata;
   int bw, bh;

   /* If input image is not 8-bit grayscale ... */
   if(id != 8){
      fprintf(stderr, "ERROR : get_minutiae : input image pixel ");
      fprintf(stderr, "depth = %d != 8.\n", id);
      return(-2);
   }

   /* Detect minutiae in grayscale fingerpeint image. */
   if((ret = lfs_detect_minutiae_V2(&minutiae,
                                   &direction_map, &low_contrast_map,
                                   &low_flow_map, &high_curve_map,
                                   &map_w, &map_h,
                                   &bdata, &bw, &bh,
                                   idata, iw, ih, lfsparms))){
      return(ret);
   }

   /* Build integrated quality map. */
   if((ret = gen_quality_map(&quality_map,
                            direction_map, low_contrast_map,
                            low_flow_map, high_curve_map, map_w, map_h))){
      free_minutiae(minutiae);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      free(bdata);
      return(ret);
   }

   /* Assign reliability from quality map. */
   if((ret = combined_minutia_quality(minutiae, quality_map, map_w, map_h,
                                     lfsparms->blocksize,
                                     idata, iw, ih, id, ppmm))){
      free_minutiae(minutiae);
      free(direction_map);
      free(low_contrast_map);
      free(low_flow_map);
      free(high_curve_map);
      free(quality_map);
      free(bdata);
      return(ret);
   }

   /* Set output pointers. */
   *ominutiae = minutiae;
   *oquality_map = quality_map;
   *odirection_map = direction_map;
   *olow_contrast_map = low_contrast_map;
   *olow_flow_map = low_flow_map;
   *ohigh_curve_map = high_curve_map;
   *omap_w = map_w;
   *omap_h = map_h;
   *obdata = bdata;
   *obw = bw;
   *obh = bh;
   *obd = id;

   /* Return normally. */
   return(0);
}
