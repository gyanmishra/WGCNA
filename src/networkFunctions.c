/*
 *
 Copyright (C) 2008 Peter Langfelder; parts based on R by R Development team

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 */


#include <stdio.h>
#include <stdlib.h>

#include <sys/time.h>

#include <R.h>
#include <R_ext/BLAS.h>
#include <R_ext/libextern.h>
#define LDOUBLE 	long double

#include "pivot.h"

#include "corFunctions-common.h"
#include "corFunctions.h"

/* =============================================================================================
 *
 * tomSimilarity
 *
 * =============================================================================================

     * expr: the expression matrix, array of nSamples * nGenes doubles
             (NA's allowed)
     corType: CorTypePearson, CorTypeBicor, CorTypeSpearman
     tomType: TomTypeNone, TomTypeUnsigned, TomTypeSigned

*/

enum { CorTypePearson = 0, CorTypeBicor = 1, CorTypeSpearman = 2 };
enum { TomTypeNone = 0, TomTypeUnsigned = 1, TomTypeSigned = 2 };
enum { TomDenomMin = 0, TomDenomMean = 1 };
enum { AdjTypeUnsigned = 0, AdjTypeSigned = 1, AdjTypeHybrid = 2, AdjTypeUnsignedKeepSign = 3 };

#define MxStr	200
typedef char	string[MxStr];
const string 	AdjErrors[] = {"No error. Just a placeholder.",
                               "Standard deviation of some genes is zero.",
                               "Unrecognized correlation type.",
                               "Unrecognized adjacency type."};

//===============================================================================================

// adjacency 

//===============================================================================================

void adjacency(double * expr, int nSamples, int nGenes, int corType, int adjType, double power, 
               double maxPOutliers, double quick, int fallback, int cosine, 
               double * adj, int * errCode, int * nThreads, int verbose, int indent)
{
  Rboolean	sd0 = FALSE;
  int 	nElems = nGenes * nGenes;
  int 	nNA = 0;

  // Rprintf("Received nGenes: %d, nSamples: %d\n", nGenes, nSamples);

  // Rprintf("adjType: %d\n", adjType);
  int err = 0;
  switch (corType)
  {
     case CorTypePearson :
        // Rprintf("Calling cor_pairwise1...");
        cor1Fast(expr, &nSamples, &nGenes, &quick, &cosine, adj, &nNA, &err, nThreads, &verbose, &indent);
        // Rprintf("..done.\n");
        if (sd0)
        {
          // Rprintf("sd0: %d\n", sd0);
          * errCode = 1;
          return;
        }
        break;
     case CorTypeBicor :
        // Rprintf("Calling bicor1...");
        bicor1Fast(expr, &nSamples, &nGenes, &maxPOutliers, &quick, &fallback, &cosine, adj, &nNA, &err, 
                   nThreads, &verbose, &indent);
        // Rprintf("..done.\n");
        if (nNA > 0)
        {
          // Rprintf("nNA: %d\n", nNA);
          * errCode = 1;
          return;
        }
        if (err>0)
        {
          // Rprintf("bicor1 returned err: %d\n", err);
          * errCode = 3;
          return;
        }
        break;
     default : 
        * errCode = 2;
        return;
  }

  // Rprintf("ADJ 1\n");

  switch (adjType) 
  {
    case AdjTypeUnsigned :
       for (int i=0; i < nElems; i++)
          adj[i] = pow(fabs(adj[i]), power);
       break;
    case AdjTypeUnsignedKeepSign : 
       for (int i=0; i < nElems; i++)
         adj[i] = (signbit(adj[i])? -1: 1) * pow(fabs(adj[i]), power);
       break;
    case AdjTypeSigned :
       for (int i=0; i < nElems; i++)
          adj[i] = pow((1+adj[i])/2, power);
       break;
    case AdjTypeHybrid :
       for (int i=0; i < nElems; i++)
          adj[i] = adj[i] > 0 ? pow(adj[i], power) : 0;
       break;
    default : 
       * errCode = 3;
  }
       
}
    
void testAdjacency(double * expr, int * nSamples, int * nGenes, int * corType, int * adjType, 
                   double * power, double * maxPOutliers, double * quick, int * fallback, int * cosine, 
                   double * adj, 
                   int * errCode, int * nThreads)
{
 adjacency(expr, * nSamples, * nGenes, * corType, * adjType, * power, 
           *maxPOutliers, *quick, *fallback, *cosine, 
           adj, errCode, nThreads, 1, 0);
}


//===========================================================================================
//
// tomSimilarity
//
//===========================================================================================

void tomSimilarity(double * expr, int * nSamples, int * nGenes, 
                   int * corType, 
                   int * adjType,
                   double * power, 
                   int * tomType, 
                   int * denomType,
                   double * maxPOutliers, 
                   double * quick,
                   int * fallback,
                   int * cosine,
                   double * tom, 
                   int * nThreads,
                   int * verbose, int * indent)
{
  double 	* adj;

  int		ng = *nGenes, ns = *nSamples;
  int 		matSize = ng * ng;

  int 		err = 0;

  char		spaces[2* *indent+1];

  for (int i=0; i<2* *indent; i++) spaces[i] = ' ';
  spaces[2* *indent] = '\0';

  int size=4000;
  int success = 0;
  double * pt;
  while ( (pt=malloc(size*size*sizeof(double)))!=NULL )
  {
    size+=1000;
    success = 1;
    free(pt);
  }

  size -= 1000;
  if ((*verbose > 0) && success) 
     printf("%sRough guide to maximum array size: about %d x %d array of doubles..\n", 
            spaces, size, size);

  if (*verbose > 0) Rprintf("%sTOM calculation: adjacency..\n", spaces);
  if (* tomType==TomTypeNone)  // just calculate adjacency.
  {
    adjacency(expr, ns, ng, *corType, *adjType, *power, *maxPOutliers, *quick, *fallback, *cosine, 
              tom, &err, nThreads, *verbose, *indent);
    if (*verbose > 0) Rprintf("\n");
    if (err) error(AdjErrors[err]);
    return;
  }
    
  if ((adj = malloc(matSize * sizeof(double))) == NULL)
    error("Memmory allocation error.");

  if ((* tomType == TomTypeSigned) && (* adjType == AdjTypeUnsigned))
    * adjType = AdjTypeUnsignedKeepSign;

  if ((* tomType == TomTypeUnsigned) && (* adjType == AdjTypeUnsignedKeepSign))
    * adjType = AdjTypeUnsigned;

  adjacency(expr, ns, ng, * corType, * adjType, * power, * maxPOutliers, * quick, *fallback, *cosine, 
            adj, & err, nThreads, *verbose, *indent);

  // Rprintf("TOM 1\n");
  if (err) 
  {
     Rprintf("TOM: exit because adjacency set err.\n");
     free(adj);
     error(AdjErrors[err]);
  }

  // Rprintf("TOM 2\n");
  double * conn;
  // Rprintf("TOM allocating memory for conn..\n");
  if ( (conn = malloc(ng * sizeof(double))) == NULL)
  {
    // Rprintf("TOM 3\n");
    free(adj);
    error("Memmory allocation error (connectivity)");
  }

  // Rprintf("    ..done\n");
  if (*verbose > 0) Rprintf("%s..connectivity..\n", spaces);
  for (int gene = 0; gene < ng; gene++)
  {
    double * adj2 = (adj + gene * ng);
    double sum = 0.0;
    for (int g2 = 0; g2 < ng; g2++)
      sum += fabs(adj2[g2]);
    conn[gene] = sum;
  }

  //Rprintf("checking diagonal adjacencies..");
  //double sum = 0.0;
  //for (int elem = 0; elem < ng; elem++) sum += adj[elem*ng + elem]-1;
  //Rprintf("sum(diag-1)=%e", sum);

  if (*verbose > 0) Rprintf("%s..matrix multiply..\n", spaces);
  double alpha = 1.0, beta = 0.0;
  dsyrk_("L", "N", nGenes, nGenes, & alpha, adj, nGenes, 
         & beta, tom, nGenes);

  if (*verbose > 0) Rprintf("%s..normalize..\n", spaces);
  double * tom2;
  double * adj2;
  int ng1 = ng-1;
  int nAbove1 = 0;
  switch (* tomType)
  {
    case TomTypeUnsigned:
      for (int j=0; j< ng1; j++)
      {
        tom2 = tom + (ng+1)*j + 1;  
        adj2 = adj + (ng+1)*j + 1;
        for (int i=j+1; i< ng; i++)
        {
          double den1;
          if ((* denomType) == TomDenomMin)
             den1 = fmin(conn[i], conn[j]);
          else
             den1 = (conn[i] + conn[j])/2;
          *tom2 = ( *tom2 - *adj2) / ( den1 - * adj2 ) ;
          *(tom + ng*i + j) = *tom2;
          if (*tom2 > 1) nAbove1++;
          tom2++;
          adj2++;
        }
      }
      break;
    case TomTypeSigned:
      for (int j=0; j< ng1; j++)
      {
        tom2 = tom + (ng+1)*j + 1;  
        adj2 = adj + (ng+1)*j + 1;
        for (int i=j+1; i< ng; i++)
        {
          double den1;
          if ((* denomType) == TomDenomMin)
             den1 = fmin(conn[i], conn[j]);
          else
             den1 = (conn[i] + conn[j])/2;
          *tom2 = fabs( *tom2 - *adj2) / ( den1 - fabs(*adj2) );
          if (*tom2 > 1) 
          {
	    Rprintf("TOM greater than 1: actual value: %f, i: %d, j: %d\n", *tom2, i, j);
	    nAbove1++;
	  }
          *(tom + ng*i + j) = *tom2;
          tom2++;
          adj2++;
        }
      }
      break;
  }
  if (nAbove1 > 0)
    Rprintf("problem: %d adjacencies are larger than 1.\n", nAbove1);

  free(conn);
  free(adj);
  if (*verbose > 0) Rprintf("%s..done.\n", spaces);
}


//====================================================================================================

// TOM similarity from adjacency

//====================================================================================================

void tomSimilarityFromAdj(double * adj, int * nGenes, 
                   int * tomType, int * denomType, double * tom, 
                   int * verbose, int * indent)
{
  int		ng = *nGenes;
  // int 		err = 0;

  char		spaces[2* *indent+1];

  for (int i=0; i<2* *indent; i++) spaces[i] = ' ';
  spaces[2* *indent] = '\0';

  double * conn;
  if ( (conn = malloc(ng * sizeof(double))) == NULL)
    error("Memmory allocation error (connectivity)");

  if (*verbose > 0) Rprintf("%s..connectivity..\n", spaces);
  for (int gene = 0; gene < ng; gene++)
  {
    double * adj2 = (adj + gene * ng);
    double sum = 0.0;
    for (int g2 = 0; g2 < ng; g2++)
      sum += fabs(adj2[g2]);
    conn[gene] = sum;
  }

  if (*verbose > 0) Rprintf("%s..matrix multiply..\n", spaces);
  double alpha = 1.0, beta = 0.0;
  dsyrk_("L", "N", nGenes, nGenes, & alpha, adj, nGenes, 
         & beta, tom, nGenes);

  if (*verbose > 0) Rprintf("%s..normalize..\n", spaces);
  // Rprintf("Using denomType %d\n", *denomType);
  double * tom2;
  double * adj2;
  int ng1 = ng-1;
  int nAbove1 = 0;
  switch (* tomType)
  {
    case TomTypeUnsigned:
      for (int j=0; j< ng1; j++)
      {
        tom2 = tom + (ng+1)*j + 1;  
        adj2 = adj + (ng+1)*j + 1;
        for (int i=j+1; i< ng; i++)
        {
          double den1;
          if ((* denomType) == TomDenomMin)
             den1 = fmin(conn[i], conn[j]);
          else
             den1 = (conn[i] + conn[j])/2;
          *tom2 = ( *tom2 - *adj2) / ( den1 - * adj2 ) ;
          *(tom + ng*i + j) = *tom2;
          if (*tom2 > 1) nAbove1++;
          tom2++;
          adj2++;
        }
      }
      break;
    case TomTypeSigned:
      for (int j=0; j< ng1; j++)
      {
        tom2 = tom + (ng+1)*j + 1;  
        adj2 = adj + (ng+1)*j + 1;
        for (int i=j+1; i< ng; i++)
        {
          double den1;
          if ((* denomType) == TomDenomMin)
             den1 = fmin(conn[i], conn[j]);
          else
             den1 = (conn[i] + conn[j])/2;
          *tom2 = fabs( *tom2 - *adj2) / ( den1 - fabs(*adj2) );
          if (*tom2 > 1) 
          {
	    Rprintf("TOM greater than 1: actual value: %f, i: %d, j: %d\n", *tom2, i, j);
	    nAbove1++;
	  }
          *(tom + ng*i + j) = *tom2;
          tom2++;
          adj2++;
        }
      }
      break;
  }
  if (nAbove1 > 0)
    Rprintf("problem: %d TOM entries are larger than 1.\n", nAbove1);

  free(conn);
  if (*verbose > 0) Rprintf("%s..done.\n", spaces);
}

/*

  Function returning the column-wise minimum and minimum index. For easier integration with R, the index
will also be stored as a double. Caution, NAs will trip this function up.

*/

void minWhichMin(double * matrix, int * nRows, int * nColumns, double * min, double * whichMin)
{
  int nrows = *nRows, ncols = *nColumns;

  for (int i=0; i<ncols; i++)
  {
    double * col = matrix + i*nrows;
    double curmin = *col;
    double curwhich = 0;
    for (int j=1; j<nrows; j++)
    {
      col++;
      if (*col < curmin) { curmin = *col; curwhich = (double) j; }
    }
    min[i] = curmin;
    whichMin[i] = curwhich;
  }
}

/*

  Function returning the column-wise mean. NAs are ignored.

*/

void mean(double * matrix, int * nRows, int * nColumns, double * mean)
{
  int nrows = *nRows, ncols = *nColumns;

  for (int i=0; i<ncols; i++)
  {
    double * col = matrix + i*nrows;
    double sum = 0;
    int count = 0;
    for (int j=1; j<nrows; j++)
    {
      col++;
      if (!ISNAN(*col))
      {
        sum += *col;
        count++;
      }
    }
    if (count > 0)
      mean[i] = sum/count;
    else
      mean[i] = NA_REAL;
  }
}

