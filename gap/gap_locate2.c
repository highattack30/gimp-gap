/* gap_locate2.c
 *    alternative implementation for locating corresponding pattern in another layer.
 *    by hof (Wolfgang Hofer)
 *  2011/12/03
 *
 */
/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/>.
 */

/* revision history:
 * version 2.7.0;             hof: created
 */

/* SYTEM (UNIX) includes */
#include "string.h"
#include "stdlib.h"

/* GIMP includes */
#include "gtk/gtk.h"
#include "libgimp/gimp.h"

/* GAP includes */
#include "gap_pixelrgn.h"
#include "gap_locate2.h"

#define MAX_DIFF_VALUE_PER_PIXEL  (255.0 + 255.0 + 255.0)
#define OPACITY_LEVEL_UCHAR 50

extern int gap_debug;

typedef struct Context {
  gint32    refShapeRadius;
  gint32    refX;
  gint32    refY;
  gint32    bestX;
  gint32    bestY;
  gint32    px;
  gint32    py;
  gint32    rowsProcessedCount;     /* debug information (for development/debug purpose) */
  gint32    cancelAttemptCount;     /* debug information (for development/debug purpose) */
  gboolean  cancelAttemptFlag;      /* indicator to cancel current evaluation attempt */
  gboolean  isFinishedFlag;         /* indicator to cancel all further evaluations */
  gint32  requiredPixelCount;       /* number of pixels minimum required for plausible area comparison  (30 % of full area size)*/
  gint32  almostFullAreaPixelCount; /* number of pixels  (90 % of full area size)*/
  gint32  involvedPixelCount;       /* number of pixels involved in current comparison attempt */
  gdouble sumDiffValue;             /* summ of the RGB channel differences of the involved pixels in the current attempt */
  gdouble currentDistance;          /* square distance from reference coords to the offset of the current attempt */
  gint32  bestMatchingPixelCount;   /* number of pixels involved in best matching attempt */
  gdouble bestMatchingDistance;     /* square distance from reference coords to the offset of the best matching attempt */
  gdouble bestMatchingSumDiffValue; /* summ of the RGB channel differences of the best matching attempt */
  gdouble veryNearDistance;         /* square of near radius to stop evaluation when exactly matching area is detected */
  GimpDrawable *refDrawable;
  GimpDrawable *targetDrawable;
  
} Context;

static gdouble     p_calculate_average_colordiff(gdouble sumDiffValue, gint32 pixelCount);
static void        p_compare_regions (const GimpPixelRgn *refPR
                    ,const GimpPixelRgn *targetPR
                    ,Context *context);
static gdouble     p_calculate_distance_to_ref_coord(Context *context, gint32 px, gint32 py);
static void        p_attempt_locate_at_current_offset(Context *context, gint32 px, gint32 py);


/* ---------------------------------
 * p_calculate_average_colordiff
 * ---------------------------------
 * calculate the average color difference for given sum of value differnces and pixel count
 */
static gdouble
p_calculate_average_colordiff(gdouble sumDiffValue, gint32 pixelCount)
{
  if (pixelCount > 0)
  {
    gdouble avgValue;
    gdouble averageColorDiff;
    
    avgValue = sumDiffValue  / (gdouble)pixelCount;
    averageColorDiff = avgValue / MAX_DIFF_VALUE_PER_PIXEL;
    return (averageColorDiff);
  }
  return (1.0);
  
}  /* end p_calculate_average_colordiff */


/* ---------------------------------
 * p_compare_regions
 * ---------------------------------
 * calculate summary Colorvalues difference for all opaque pixels
 * in the compared area region.
 */
static void
p_compare_regions (const GimpPixelRgn *refPR
                  ,const GimpPixelRgn *targetPR
                  ,Context *context)
{
  guint    row;
  guchar*  ref = refPR->data;   /* the reference drawable */
  guchar*  target = targetPR->data;   /* the target drawable */
  
  guint    commonWidth;
  guint    commonHeight;

  commonWidth = MIN(targetPR->w, refPR->w);
  commonHeight = MIN(targetPR->h, refPR->h);

//   if(gap_debug)
//   {
//     printf("region REF x:%d y:%d w:%d h:%d  TARGET x:%d y:%d w:%d h:%d  Common w:%d h:%d px:%d py:%d\n"
//        , (int)refPR->x
//        , (int)refPR->y
//        , (int)refPR->w
//        , (int)refPR->h
//        , (int)targetPR->x
//        , (int)targetPR->y
//        , (int)targetPR->w
//        , (int)targetPR->h
//        , (int)commonWidth
//        , (int)commonHeight
//        , (int)context->px
//        , (int)context->py
//        );
//   }


  for (row = 0; row < commonHeight; row++)
  {
    guint  col;
    guint  idxref;
    guint  idxtarget;
    
//    context->rowsProcessedCount += 1;
    idxref = 0;
    idxtarget = 0;
    for(col = 0; col < commonWidth; col++)
    {
      gboolean isCompareable;
      
      isCompareable = TRUE;
      
      if(refPR->bpp > 3)
      {
        if(ref[idxref +3] < OPACITY_LEVEL_UCHAR)
        {
          /* transparent reference pixel is not compared */
          isCompareable = FALSE;
        }
      }

      if(targetPR->bpp > 3)
      {
        if(target[idxtarget +3] < OPACITY_LEVEL_UCHAR)
        {
          /* transparent target pixel is not compared */
          isCompareable = FALSE;
        }
      }

      if (isCompareable == TRUE)
      {
        context->involvedPixelCount += 1;
        context->sumDiffValue += abs(ref[idxref]    - target[idxtarget]);
        context->sumDiffValue += abs(ref[idxref +1] - target[idxtarget +1]);
        context->sumDiffValue += abs(ref[idxref +2] - target[idxtarget +2]);

        
      }

      idxref    += refPR->bpp;
      idxtarget += targetPR->bpp;
    }

    /* check for early escape possibilty when current sumDiffValue gets too large 
     * Preformance note: 
     *  we could do this check already in the inner column based loop to detect escape
     *  possibilities a little earlier, but i guess
     *  overall performance might be better when cecks are done only once per row.
     */
    if (context->sumDiffValue > context->bestMatchingSumDiffValue)
    {
      if (context->bestMatchingPixelCount >= context->almostFullAreaPixelCount)
      {
        /* The (so far best matching) result was based on comparison
         * of almost all area pixels involved.
         * (Less pixels may be involved when:
         *   o) there are transparent pixels in the compared areas OR
         *   o) the compared areas are clipped at layer borders
         * )
         * At this point there is nearly no more chance for a better matching
         * average color diff in the current attempt, and we can
         * stop evaluating area at current offset for performance reasons.
         */
        context->cancelAttemptFlag = TRUE;
        //context->cancelAttemptCount += 1;
        return;
      }
      else if (context->sumDiffValue >= (context->bestMatchingSumDiffValue + context->bestMatchingSumDiffValue) )
      {
        /* the sumDiffValue is now alredy 2 times worse then the best match.
         * and the chances are low to get a better average color diff in the current attempt
         */
        context->cancelAttemptFlag = TRUE;
        //context->cancelAttemptCount += 1;
        return;
      }
    }



    ref += refPR->rowstride;
    target += targetPR->rowstride;

  }

}  /* end p_compare_regions */


/* ---------------------------------
 * p_calculate_distance_to_ref_coord
 * ---------------------------------
 * calculate the (square of) the distance between reference coordinates and px/py
 */
static gdouble
p_calculate_distance_to_ref_coord(Context *context, gint32 px, gint32 py)
{
  gdouble squareDistance;
  
  squareDistance =  (context->refX - px) * (context->refX - px);
  squareDistance += (context->refY - py) * (context->refY - py);
  
  return(squareDistance);
  
}  /* end p_calculate_distance_to_ref_coord */


/* --------------------------------------------
 * p_attempt_locate_at_current_offset
 * --------------------------------------------
 */
static void
p_attempt_locate_at_current_offset(Context *context, gint32 px, gint32 py)
{
  GimpPixelRgn refPR;
  GimpPixelRgn targetPR;
  gpointer  pr;
  gint      rx1, ry1, rWidth, rHeight;
  gint      tx1, ty1, tWidth, tHeight;
  gint      commonAreaWidth, commonAreaHeight;
  gint      fullShapeWidth,  fullShapeHeight;
  gint      rWidthRequired, rHeightRequired;
  gboolean  isIntersect;

  gint    leftShapeRadius;
  gint    upperShapeRadius;


  if (context->isFinishedFlag)
  {
    return;
  }
  
  fullShapeWidth = (2 * context->refShapeRadius);
  fullShapeHeight = (2 * context->refShapeRadius);

  /* calculate processing relevant intersecting reference / target rectangles */  

  isIntersect =
   gimp_rectangle_intersect((context->refX - context->refShapeRadius)  /* origin1 */
                          , (context->refY - context->refShapeRadius)
                          , fullShapeWidth               /*  width1 */
                          , fullShapeHeight              /* height1 */
                          ,0
                          ,0
                          ,context->refDrawable->width
                          ,context->refDrawable->height
                          ,&rx1
                          ,&ry1
                          ,&rWidth
                          ,&rHeight
                          );
  if (!isIntersect)
  {
    return;
  }
  
  leftShapeRadius = context->refX - rx1;
  upperShapeRadius = context->refY - ry1;

  isIntersect =
   gimp_rectangle_intersect((px - leftShapeRadius)  /* origin1 */
                          , (py - upperShapeRadius)
                          , rWidth               /*  width1 */
                          , rHeight               /* height1 */
                          ,0
                          ,0
                          ,context->targetDrawable->width
                          ,context->targetDrawable->height
                          ,&tx1
                          ,&ty1
                          ,&tWidth
                          ,&tHeight
                          );
  if (!isIntersect)
  {
    return;
  }
  
  commonAreaWidth = tWidth;
  commonAreaHeight = tHeight;

  // TODO test if 2/3 of the fullShapeWidth and fullShapeHeight is sufficient for usable results.
  // alternative1: maybe require  rWidth and rHeight
  // alternative2: maybe require  fullShapeWidth and fullShapeHeight
  rWidthRequired = (fullShapeWidth * 2) / 3;
  rHeightRequired = (fullShapeHeight * 2) / 3;
  
  if ((commonAreaWidth < rWidthRequired) 
  ||  (commonAreaHeight < rHeightRequired))
  {
    /* the common area is significant smaller than the reference shape 
     * skip the compare attempt in this case to avoid unpredictable results (near borders)
     */
    return;
  }

  

//   if(gap_debug)
//   {
//     printf("p_attempt_locate_at: px: %04d py:%04d\n"
//            "                     rx1:%04d ry1:%04d rWidth:%d rHeight:%d\n"
//            "                     tx1:%04d ty1:%04d tWidth:%d tHeight:%d\n"
//            "                     commonAreaWidth:%d commonAreaHeight:%d\n"
//       ,(int)px
//       ,(int)py
//       ,(int)rx1
//       ,(int)ry1
//       ,(int)rWidth
//       ,(int)rHeight
//       ,(int)tx1
//       ,(int)ty1
//       ,(int)tWidth
//       ,(int)tHeight
//       ,(int)commonAreaWidth
//       ,(int)commonAreaHeight
//       );
//   }

  /* rest 'per offset' values in the context */
  context->cancelAttemptFlag = FALSE;
  context->sumDiffValue = 0;
  context->involvedPixelCount = 0;
  context->currentDistance = p_calculate_distance_to_ref_coord(context, px, py);
  context->px = px;
  context->py = py;

  gimp_pixel_rgn_init (&refPR, context->refDrawable, rx1, ry1
                      , commonAreaWidth, commonAreaHeight
                      , FALSE     /* dirty */
                      , FALSE     /* shadow */
                       );

  gimp_pixel_rgn_init (&targetPR, context->targetDrawable, tx1, ty1
                      , commonAreaWidth, commonAreaHeight
                      , FALSE     /* dirty */
                      , FALSE     /* shadow */
                       );

  /* compare pixel areas in tiled portions via pixel region processing loops.
   */
  for (pr = gimp_pixel_rgns_register (2, &refPR, &targetPR);
       pr != NULL;
       pr = gimp_pixel_rgns_process (pr))
  {
    if (context->cancelAttemptFlag)
    {
      break;
    }
    else
    {
      p_compare_regions(&refPR, &targetPR, context);
    }
  }

  if (pr != NULL)
  {
     /* NOTE:
      * early escaping from the loop with pr != NULL
      * leads to memory leaks due to unbalanced tile ref/unref calls.
      * the call to gap_gimp_pixel_rgns_unref cals unref on the current tile
      * (in the same way as gimp_pixel_rgns_process does)
      * but does not ref another available tile.
      */
    gap_gimp_pixel_rgns_unref (pr);
  
  }
  

  
  if ((context->involvedPixelCount >= context->requiredPixelCount)
  &&  (context->sumDiffValue <= context->bestMatchingSumDiffValue))
  {
    if((context->sumDiffValue < context->bestMatchingSumDiffValue)
    || ( context->currentDistance < context->bestMatchingDistance))
    {
      context->bestMatchingSumDiffValue = context->sumDiffValue;
      context->bestMatchingDistance = context->currentDistance;
      context->bestMatchingPixelCount = context->involvedPixelCount;
      context->bestX = px;
      context->bestY = py;
 
      if(gap_debug)
      {
        gdouble bestMatchingAvgColordiff;
        
        bestMatchingAvgColordiff = p_calculate_average_colordiff(context->bestMatchingSumDiffValue
                                    , context->bestMatchingPixelCount
                                    );
        printf("FOUND: bestX:%d bestY:%d squareDist:%d\n"
               "             sumDiffValues:%d pixelCount:%d bestMatchingAvgColordiff:%.5f\n"
          , (int)context->bestX
          , (int)context->bestY
          , (int)context->bestMatchingDistance
          , (int)context->bestMatchingSumDiffValue
          , (int)context->bestMatchingPixelCount
          , (float)bestMatchingAvgColordiff
          );
      }
       
      if ((context->currentDistance <= context->veryNearDistance)
      &&  (context->sumDiffValue == 0))
      {
        /* stop all further attempts on exact matching area when near reference origin */
        context->isFinishedFlag = TRUE;
      }
    }
  }

  
}  /* end p_attempt_locate_at_current_offset */



/* --------------------------------------------
 * gap_locateAreaWithinRadiusWithOffset
 * --------------------------------------------
 * processing starts at reference coords + offest
 * and continues outwards upto targetMoveRadius for 4 quadrants.
 * 
 * returns average color difference (0.0 upto 1.0)
 *    where 0.0 indicates exact matching area
 *      and 1.0 indicates all pixel have maximum color diff (when comaring full white agains full black area)
 */
gdouble
gap_locateAreaWithinRadiusWithOffset(gint32  refDrawableId
  , gint32  refX
  , gint32  refY
  , gint32  refShapeRadius
  , gint32  targetDrawableId
  , gint32  targetMoveRadius
  , gint32  *targetX
  , gint32  *targetY
  , gint32  offsetX
  , gint32  offsetY
  )
{
  Context contextData;
  Context *context;
  gdouble averageColorDiff;
  gboolean isFinishedFlag;
  gdouble maxPixelCount;
  gint32  shapeDiameter;
  gint32  fullAreaPixelCount;
  gint32  dx;
  gint32  dy;
  
  
  *targetX = refX;
  *targetY = refY;
  
  shapeDiameter = 1 + (refShapeRadius + refShapeRadius);
  fullAreaPixelCount = (shapeDiameter) * (shapeDiameter);
  
  /* init Context */
  context = &contextData;
  context->refShapeRadius = refShapeRadius;
  context->refX = refX;
  context->refY = refY;
  context->bestX = refX;
  context->bestY = refY;
  context->cancelAttemptCount = 0;
  context->rowsProcessedCount = 0;
  context->cancelAttemptFlag = FALSE;
  context->isFinishedFlag = FALSE;
  context->requiredPixelCount = (fullAreaPixelCount * 30) / 100;
  context->almostFullAreaPixelCount = (fullAreaPixelCount * 90) / 100;
  context->involvedPixelCount = 0;
  context->sumDiffValue = 0;
  context->currentDistance = 0;
  context->bestMatchingPixelCount = 0;
  context->veryNearDistance = (2 * 2);

  context->refDrawable = gimp_drawable_get(refDrawableId);
  context->targetDrawable = gimp_drawable_get(targetDrawableId);
  
  maxPixelCount = MAX(context->refDrawable->width, context->targetDrawable->width)
                * MAX(context->refDrawable->height, context->targetDrawable->height);
                
  context->bestMatchingSumDiffValue = maxPixelCount * MAX_DIFF_VALUE_PER_PIXEL;
  context->bestMatchingDistance = maxPixelCount;
  
  averageColorDiff = 1.0;
  
  if(gap_debug)
  {
      printf("gap_locateAreaWithinRadiusWithOffset START: refDrawableId:%d targetDrawableId:%d\n"
             "                           refX:%d refY:%d refShapeRadius:%d\n"
             "                           requiredPixelCount:%d almostFullAreaPixelCount:%d  fullAreaPixelCount:%d\n"
        , (int)refDrawableId
        , (int)targetDrawableId
        , (int)context->refX
        , (int)context->refY
        , (int)refShapeRadius
        , (int)context->requiredPixelCount
        , (int)context->almostFullAreaPixelCount
        , (int)fullAreaPixelCount
        );
  }
  
  
  for(dx = 0; dx <= targetMoveRadius; dx ++)
  {
    if (context->isFinishedFlag) 
    { 
      break; 
    }

    for(dy = 0; dy <= targetMoveRadius; dy++)
    {
 
      p_attempt_locate_at_current_offset(context, (offsetX + refX) + dx, (offsetY + refY) + dy);
      if (isFinishedFlag)
      { 
        break;
      }
      
      if (dx > 0)
      {
        p_attempt_locate_at_current_offset(context, (offsetX + refX) - dx, (offsetY + refY) + dy);
        if (context->isFinishedFlag) 
        {
          break; 
        }
      }
      
      if (dy > 0)
      {
        p_attempt_locate_at_current_offset(context, (offsetX + refX) + dx, (offsetY + refY) - dy);
        if (context->isFinishedFlag) 
        {
          break; 
        }
      }
      
      if ((dx > 0) && (dy > 0))
      {
        p_attempt_locate_at_current_offset(context, (offsetX + refX) - dx, (offsetY + refY) - dy);
        if (context->isFinishedFlag) 
        {
          break; 
        }
      }
      
    }
  }
  
  if (context->bestMatchingPixelCount > 0)
  {
    *targetX = context->bestX;
    *targetY = context->bestY;
    averageColorDiff = p_calculate_average_colordiff(context->bestMatchingSumDiffValue
                                    , context->bestMatchingPixelCount
                                    );

    
    if(gap_debug)
    {
      printf("gap_locateAreaWithinRadiusWithOffset Result: bestX:%d bestY:%d averageColorDiff:%.5f\n"
             "                           sumDiffValues:%d pixelCount:%d\n"
             "                           refX:%d refY:%d  cancelAttemptCount:%d rowsProcessedCount:%d\n"
             "                           requiredPixelCount:%d almostFullAreaPixelCount:%d\n"
        , (int)context->bestX
        , (int)context->bestY
        , (float)averageColorDiff
        , (int)context->bestMatchingSumDiffValue
        , (int)context->bestMatchingPixelCount
        , (int)context->refX
        , (int)context->refY
        , (int)context->cancelAttemptCount
        , (int)context->rowsProcessedCount
        , (int)context->requiredPixelCount
        , (int)context->almostFullAreaPixelCount
        );
    }
  }
  else
  {
    if(gap_debug)
    {
      printf("gap_locateAreaWithinRadiusWithOffset * NOTHING FOUND *\n");
    }
  }


  if(context->refDrawable != NULL)
  {
    gimp_drawable_detach(context->refDrawable);
  }
  if(context->targetDrawable != NULL)
  {
    gimp_drawable_detach(context->targetDrawable);
  }

  return (averageColorDiff);

}  /* end gap_locateAreaWithinRadiusWithOffset */


/* --------------------------------------------
 * gap_locateAreaWithinRadius
 * --------------------------------------------
 * processing starts at reference coords and continues
 * outwards upto targetMoveRadius for 4 quadrants.
 * 
 * returns average color difference (0.0 upto 1.0)
 *    where 0.0 indicates exact matching area
 *      and 1.0 indicates all pixel have maximum color diff (when comaring full white agains full black area)
 */
gdouble
gap_locateAreaWithinRadius(gint32  refDrawableId
  , gint32  refX
  , gint32  refY
  , gint32  refShapeRadius
  , gint32  targetDrawableId
  , gint32  targetMoveRadius
  , gint32  *targetX
  , gint32  *targetY
  )
{
  gdouble avgColordiff;
  
  avgColordiff =
    gap_locateAreaWithinRadiusWithOffset(refDrawableId
       , refX
       , refY
       , refShapeRadius
       , targetDrawableId
       , targetMoveRadius
       , targetX
       , targetY
       , 0
       , 0
       );
  return (avgColordiff);
  
}  /* end gap_locateAreaWithinRadius */
