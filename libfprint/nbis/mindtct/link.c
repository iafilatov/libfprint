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

      FILE:    LINK.C
      AUTHOR:  Michael D. Garris
      DATE:    08/02/1999
      UPDATED: 10/04/1999 Version 2 by MDG
      UPDATED: 03/16/2005 by MDG

      Contains routines responsible for linking compatible minutiae
      together as part of the NIST Latent Fingerprint System (LFS).

***********************************************************************
               ROUTINES:
                        link_minutiae()
                        create_link_table()
                        update_link_table()
                        order_link_table()
                        process_link_table()
                        link_score()
***********************************************************************/

#include <stdio.h>
#include <lfs.h>
#include <log.h>

/*************************************************************************
**************************************************************************
#cat: link_minutiae - Clusters minutiae that are sufficiently close to each
#cat:                other and have compatible directions to be considered part
#cat:                of the same ridge or valley and then links them together.
#cat:                In linking two minutia, the respective minutia features
#cat:                in the image are joined by drawing pixels and the points
#cat:                are removed from the list.

   Input:
      minutiae  - list of true and false minutiae
      bdata     - binary image data (0==while & 1==black)
      iw        - width (in pixels) of image
      ih        - height (in pixels) of image
      nmap      - IMAP ridge flow matrix with invalid, high-curvature,
                  and no-valid-neighbor regions identified
      mw        - width in blocks of the NMAP
      mh        - height in blocks of the NMAP
      lfsparms  - parameters and thresholds for controlling LFS
   Output:
      minutiae  - list of pruned minutiae
      bdata     - edited binary image with breaks in ridges and valleys filled
   Return Code:
      Zero     - successful completion
      Negative - system error
**************************************************************************/

/*************************************************************************
**************************************************************************
#cat: create_link_table - Builds a 2D minutia link table where each cell in the
#cat:                    table represents a potential linking of 2 different
#cat:                    minutia points.  Minutia IDs are stored on each axes
#cat:                    and scores representing the degree of compatibility
#cat:                    between 2 minutia are stored in each cell.  Note that
#cat:                    the table is sparsely filled with scores.

   Input:
      tbldim    - dimension of each axes of the link table
      start     - index position of starting minutia point in input list
      minutiae  - list of minutia
      onloop    - list of loop flags (on flag for each minutia point in list)
      nmap      - IMAP ridge flow matrix with invalid, high-curvature,
                  and no-valid-neighbor regions identified
      mw        - width in blocks of the NMAP
      mh        - height in blocks of the NMAP
      bdata     - binary image data (0==while & 1==black)
      iw        - width (in pixels) of image
      ih        - height (in pixels) of image
      lfsparms  - parameters and thresholds for controlling LFS
   Output:
      olink_table - sparse 2D table containing scores of potentially
                    linked minutia pairs
      ox_axis     - minutia IDs registered along x-axis
      oy_axis     - minutia IDs registered along y-axis
      onx_axis    - number of minutia registered along x-axis
      ony_axis    - number of minutia registered along y-axis
      on_entries  - number of scores currently entered in the table
   Return Code:
      Zero      - successful completion
      Negative  - system error
**************************************************************************/

/*************************************************************************
**************************************************************************
#cat: update_link_table - Takes the indices of 2 minutia and their link
#cat:                    compatibility score and updates the 2D link table.
#cat:                    The input minutia are registered to positions along
#cat:                    different axes, if they are not already in the table,
#cat:                    and a queue is maintained so that a cluster of
#cat:                    potentially linked points may be gathered.

   Input:
      link_table - sparse 2D table containing scores of potentially linked
                   minutia pairs
      x_axis     - minutia IDs registered along x-axis
      y_axis     - minutia IDs registered along y-axis
      nx_axis    - number of minutia registered along x-axis
      ny_axis    - number of minutia registered along y-axis
      n_entries  - number of scores currently entered in the table
      tbldim     - dimension of each axes of the link table
      queue      - list of clustered minutiae yet to be used to locate
                   other compatible minutiae
      head       - head of the queue
      tail       - tail of the queue
      inqueue    - flag for each minutia point in minutiae list to signify if
                   it has been clustered with the points in this current link
                   table
      first      - index position of first minutia of current link pair
      second     - index position of second minutia of current link pair
      score      - degree of link compatibility of current link pair
   Output:
      link_table - updated sparse 2D table containing scores of potentially
                   linked minutia pairs
      x_axis     - updated minutia IDs registered along x-axis
      y_axis     - updated minutia IDs registered along y-axis
      nx_axis    - updated number of minutia registered along x-axis
      ny_axis    - updated number of minutia registered along y-axis
      n_entries  - updated number of scores currently entered in the table
      queue      - updated list of clustered minutiae yet to be used to locate
                   other compatible minutiae
      tail       - updated tail of the queue
      inqueue    - updated list of flags, one for each minutia point in
                   minutiae list to signify if it has been clustered with
                   the points in this current link table
   Return Code:
      Zero      - successful completion
      Negative  - system error
**************************************************************************/

/*************************************************************************
**************************************************************************
#cat: order_link_table - Puts the link table in sorted order based on x and
#cat:                    then y-axis entries.  These minutia are sorted based
#cat:                    on their point of perpendicular intersection with a
#cat:                    line running from the origin at an angle equal to the
#cat:                    average direction of all entries in the link table.

   Input:
      link_table - sparse 2D table containing scores of potentially linked
                   minutia pairs
      x_axis     - minutia IDs registered along x-axis
      y_axis     - minutia IDs registered along y-axis
      nx_axis    - number of minutia registered along x-axis
      ny_axis    - number of minutia registered along y-axis
      n_entries  - number of scores currently entered in the table
      tbldim     - dimension of each axes of the link table
      minutiae   - list of minutia
      ndirs      - number of IMAP directions (in semicircle)
   Output:
      link_table - sorted sparse 2D table containing scores of potentially
                   linked minutia pairs
      x_axis     - sorted minutia IDs registered along x-axis
      y_axis     - sorted minutia IDs registered along y-axis
   Return Code:
      Zero      - successful completion
      Negative  - system error
**************************************************************************/

/*************************************************************************
**************************************************************************
#cat: process_link_table - Processes the link table deciding which minutia
#cat:                      pairs in the table should be linked (ie. joined in
#cat:                      the image and removed from the minutiae list (and
#cat:                      from onloop).

   Input:
      link_table - sparse 2D table containing scores of potentially linked
                   minutia pairs
      x_axis     - minutia IDs registered along x-axis
      y_axis     - minutia IDs registered along y-axis
      nx_axis    - number of minutia registered along x-axis
      ny_axis    - number of minutia registered along y-axis
      n_entries  - number of scores currently entered in the table
      tbldim     - dimension of each axes of the link table
      minutiae   - list of minutia
      onloop     - list of flags signifying which minutia lie on small lakes
      bdata      - binary image data (0==while & 1==black)
      iw         - width (in pixels) of image
      ih         - height (in pixels) of image
      lfsparms   - parameters and thresholds for controlling LFS
   Output:
      minutiae   - list of pruned minutiae
      onloop     - updated loop flags
      bdata      - edited image with minutia features joined
   Return Code:
      Zero      - successful completion
      Negative  - system error
**************************************************************************/

/*************************************************************************
**************************************************************************
#cat: link_score - Takes 2 parameters, a 'join angle' and a 'join distance'
#cat:              computed between 2 minutia and combines these to compute
#cat:              a score representing the degree of link compatibility
#cat:              between the 2 minutiae.

   Input:
      jointheta - angle measured between 2 minutiae
      joindist  - distance between 2 minutiae
      lfsparms  - parameters and thresholds for controlling LFS
   Return Code:
      Score     - degree of link compatibility
**************************************************************************/

