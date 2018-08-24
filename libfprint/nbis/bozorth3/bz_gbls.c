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
      LIBRARY: FING - NIST Fingerprint Systems Utilities

      FILE:           BZ_GBLS.C
      ALGORITHM:      Allan S. Bozorth (FBI)
      MODIFICATIONS:  Michael D. Garris (NIST)
                      Stan Janet (NIST)
      DATE:           09/21/2004

      Contains global variables responsible for supporting the
      Bozorth3 fingerprint matching "core" algorithm.

***********************************************************************
***********************************************************************/

#include <bozorth.h>

/**************************************************************************/
/* General supporting global variables */
/**************************************************************************/

int colp[ COLP_SIZE_1 ][ COLP_SIZE_2 ];		/* Output from match(), this is a sorted table of compatible edge pairs containing: */
						/*	DeltaThetaKJs, Subject's K, J, then On-File's {K,J} or {J,K} depending */
						/* Sorted first on Subject's point index K, */
						/*	then On-File's K or J point index (depending), */
						/*	lastly on Subject's J point index */
int scols[ SCOLS_SIZE_1 ][ COLS_SIZE_2 ];	/* Subject's pointwise comparison table containing: */
						/*	Distance,min(BetaK,BetaJ),max(BetaK,BbetaJ), K,J,ThetaKJ */
int fcols[ FCOLS_SIZE_1 ][ COLS_SIZE_2 ];	/* On-File Record's pointwise comparison table with: */
						/*	Distance,min(BetaK,BetaJ),max(BetaK,BbetaJ),K,J, ThetaKJ */
int * scolpt[ SCOLPT_SIZE ];			/* Subject's list of pointers to pointwise comparison rows, sorted on: */
						/*	Distance, min(BetaK,BetaJ), then max(BetaK,BetaJ) */
int * fcolpt[ FCOLPT_SIZE ];			/* On-File Record's list of pointers to pointwise comparison rows sorted on: */
						/*	Distance, min(BetaK,BetaJ), then max(BetaK,BetaJ) */
int sc[ SC_SIZE ];				/* Flags all compatible edges in the Subject's Web */

int yl[ YL_SIZE_1 ][ YL_SIZE_2 ];


/**************************************************************************/
/* Globals used significantly by sift() */
/**************************************************************************/
#ifdef TARGET_OS
   int rq[ RQ_SIZE ];
   int tq[ TQ_SIZE ];
   int zz[ ZZ_SIZE ];

   int rx[ RX_SIZE ];
   int mm[ MM_SIZE ];
   int nn[ NN_SIZE ];

   int qq[ QQ_SIZE ];

   int rk[ RK_SIZE ];

   int cp[ CP_SIZE ];
   int rp[ RP_SIZE ];

   int rf[RF_SIZE_1][RF_SIZE_2];
   int cf[CF_SIZE_1][CF_SIZE_2];

   int y[20000];
#else
   int rq[ RQ_SIZE ] = {};
   int tq[ TQ_SIZE ] = {};
   int zz[ ZZ_SIZE ] = {};

   int rx[ RX_SIZE ] = {};
   int mm[ MM_SIZE ] = {};
   int nn[ NN_SIZE ] = {};

   int qq[ QQ_SIZE ] = {};

   int rk[ RK_SIZE ] = {};

   int cp[ CP_SIZE ] = {};
   int rp[ RP_SIZE ] = {};

   int rf[RF_SIZE_1][RF_SIZE_2] = {};
   int cf[CF_SIZE_1][CF_SIZE_2] = {};

   int y[20000] = {};
#endif

