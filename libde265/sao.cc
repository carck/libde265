/*
 * H.265 video codec.
 * Copyright (c) 2013-2014 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of libde265.
 *
 * libde265 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libde265 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libde265.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sao.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>


void apply_sao(de265_image* img, int xCtb,int yCtb,
               const slice_segment_header* shdr, int cIdx, int nS,
               const uint8_t* in_img,  int in_stride,
               /* */ uint8_t* out_img, int out_stride)
{
  const seq_parameter_set* sps = &img->sps;
  const pic_parameter_set* pps = &img->pps;
  int bitDepth = (cIdx==0 ? sps->BitDepth_Y : sps->BitDepth_C);
  int maxPixelValue = (1<<bitDepth)-1;

  int xC = xCtb*nS;
  int yC = yCtb*nS;

  const sao_info* saoinfo = img->get_sao_info(xCtb,yCtb);

  int SaoTypeIdx = (saoinfo->SaoTypeIdx >> (2*cIdx)) & 0x3;

  logtrace(LogSAO,"apply_sao CTB %d;%d cIdx:%d type=%d (%dx%d)\n",xC,yC,cIdx, SaoTypeIdx, nS,nS);

  if (SaoTypeIdx==0) {
    return;
  }

  /*
    if ((sps->pcm_loop_filter_disable_flag && get_pcm_flag(ctx->img,sps,xC,yC)) ||
    get_cu_transquant_bypass(ctx->img,sps,xC,yC) ||
    SaoTypeIdx == 0)
    {
    return;
    }
  */

  int width  = sps->pic_width_in_luma_samples;
  int height = sps->pic_height_in_luma_samples;

  if (cIdx>0) { width =(width+1)/2; height =(height+1)/2; }

  int ctbSliceAddrRS = img->get_SliceHeader(xC,yC)->SliceAddrRS;
  const std::vector<int>& MinTbAddrZS = pps->MinTbAddrZS;
  int  PicWidthInTbsY = sps->PicWidthInTbsY;
  int  Log2MinTrafoSize = sps->Log2MinTrafoSize;
  int  chromaLog2MinTrafoSize = Log2MinTrafoSize;
  if (cIdx>0) { chromaLog2MinTrafoSize-=1; }

  int picWidthInCtbs = sps->PicWidthInCtbsY;
  int  ctbshift = sps->Log2CtbSizeY;
  int  chromashift = 0;
  if (cIdx>0) { ctbshift-=1; chromashift=1; }


  /*
  uint8_t* out_img;
  int out_stride;
  out_img = img->get_image_plane(cIdx);
  out_stride = img->get_image_stride(cIdx);
  */

  for (int i=0;i<5;i++)
    {
      logtrace(LogSAO,"offset[%d] = %d\n", i, i==0 ? 0 : saoinfo->saoOffsetVal[cIdx][i-1]);
    }


  int ctbW = nS;
  int ctbH = nS;
  if (xC+ctbW>width)  ctbW = width -xC;
  if (yC+ctbH>height) ctbH = height-yC;


  const bool extendedTests = (img->get_CTB_has_pcm(xCtb,yCtb) ||
                              img->get_CTB_has_cu_transquant_bypass(xCtb,yCtb));

  if (SaoTypeIdx==2) {
    int hPos[2], vPos[2];
    int vPosStride[2]; // vPos[] multiplied by image stride
    int SaoEoClass = (saoinfo->SaoEoClass >> (2*cIdx)) & 0x3;

    //logtrace(LogSAO,"SaoEoClass = %d\n", SaoEoClass);

    switch (SaoEoClass) {
    case 0: hPos[0]=-1; hPos[1]= 1; vPos[0]= 0; vPos[1]=0; break;
    case 1: hPos[0]= 0; hPos[1]= 0; vPos[0]=-1; vPos[1]=1; break;
    case 2: hPos[0]=-1; hPos[1]= 1; vPos[0]=-1; vPos[1]=1; break;
    case 3: hPos[0]= 1; hPos[1]=-1; vPos[0]=-1; vPos[1]=1; break;
    }

    vPosStride[0] = vPos[0] * in_stride;
    vPosStride[1] = vPos[1] * in_stride;

    /* Reorder sao_info.saoOffsetVal[] array, so that we can index it
       directly with the sum of the two pixel-difference signs. */
    int8_t  saoOffsetVal[5]; // [2] unused
    saoOffsetVal[0] = saoinfo->saoOffsetVal[cIdx][1-1];
    saoOffsetVal[1] = saoinfo->saoOffsetVal[cIdx][2-1];
    saoOffsetVal[3] = saoinfo->saoOffsetVal[cIdx][3-1];
    saoOffsetVal[4] = saoinfo->saoOffsetVal[cIdx][4-1];


    for (int j=0;j<ctbH;j++) {
      const uint8_t* in_ptr  = &in_img [xC+(yC+j)*in_stride];
      /* */ uint8_t* out_ptr = &out_img[xC+(yC+j)*out_stride];

      for (int i=0;i<ctbW;i++) {
        int edgeIdx = -1;

        logtrace(LogSAO, "pos %d,%d\n",xC+i,yC+j);

        if (extendedTests &&
            (sps->pcm_loop_filter_disable_flag &&
             img->get_pcm_flag((xC+i)<<chromashift,(yC+j)<<chromashift)) ||
            img->get_cu_transquant_bypass((xC+i)<<chromashift,(yC+j)<<chromashift)) {
          continue;
        }

        // do the expensive test for boundaries only at the boundaries
        bool testBoundary = (i==0 || j==0 || i==ctbW-1 || j==ctbH-1);

        if (testBoundary)
          for (int k=0;k<2;k++) {
            int xS = xC+i+hPos[k];
            int yS = yC+j+vPos[k];

            if (xS<0 || yS<0 || xS>=width || yS>=height) {
              edgeIdx=0;
              break;
            }


            // This part seems inefficient with all the get_SliceHeaderIndex() calls,
            // but removing this part (because the input was known to have only a single
            // slice anyway) reduced computation time only by 1.3%.
            // TODO: however, this may still be a big part of SAO itself.

            int sliceAddrRS = img->get_SliceHeader(xS<<chromashift,yS<<chromashift)->SliceAddrRS;
            if (sliceAddrRS <  ctbSliceAddrRS && // much simpler test that the two conditions below
                //sliceAddrRS != ctbSliceAddrRS &&
                //MinTbAddrZS[( xS   >>Log2MinTrafoSize) +  (yS   >>Log2MinTrafoSize)*PicWidthInTbsY] <
                //MinTbAddrZS[((xC+i)>>Log2MinTrafoSize) + ((yC+j)>>Log2MinTrafoSize)*PicWidthInTbsY] &&
                img->get_SliceHeader((xC+i)<<chromashift,(yC+j)<<chromashift)->slice_loop_filter_across_slices_enabled_flag==0) {
              edgeIdx=0;
              break;
            }

            if (sliceAddrRS >  ctbSliceAddrRS && // much simpler test that the two conditions below
                //sliceAddrRS != ctbSliceAddrRS &&
                //MinTbAddrZS[((xC+i)>>Log2MinTrafoSize) + ((yC+j)>>Log2MinTrafoSize)*PicWidthInTbsY] <
                //MinTbAddrZS[( xS   >>Log2MinTrafoSize) +  (yS   >>Log2MinTrafoSize)*PicWidthInTbsY] &&
                img->get_SliceHeader(xS<<chromashift,yS<<chromashift)->slice_loop_filter_across_slices_enabled_flag==0) {
              edgeIdx=0;
              break;
            }


            if (pps->loop_filter_across_tiles_enabled_flag==0 && 
                pps->TileIdRS[(xS>>ctbshift) + (yS>>ctbshift)*picWidthInCtbs] !=
                pps->TileIdRS[(xC>>ctbshift) + (yC>>ctbshift)*picWidthInCtbs]) {
              edgeIdx=0;
              break;
            }
          }

        if (edgeIdx != 0) {

          edgeIdx = ( Sign(in_ptr[i] - in_ptr[i+hPos[0]+vPosStride[0]]) +
                      Sign(in_ptr[i] - in_ptr[i+hPos[1]+vPosStride[1]])   );

          if (edgeIdx != 0) {
            int offset = saoOffsetVal[edgeIdx+2];

            out_ptr[i] = Clip3(0,maxPixelValue,
                               in_ptr[i] + offset);
          }
        }
      }
    }
  }
  else {
    int bandShift = bitDepth-5;
    int saoLeftClass = saoinfo->sao_band_position[cIdx];
    logtrace(LogSAO,"saoLeftClass: %d\n",saoLeftClass);

    int bandTable[32];
    memset(bandTable, 0, sizeof(int)*32);

    for (int k=0;k<4;k++) {
      bandTable[ (k+saoLeftClass)&31 ] = k+1;
    }


    /* If PCM or transquant_bypass is used in this CTB, we have to
       run all checks (A).
       Otherwise, we run a simplified version of the code (B).

       NOTE: this whole part of SAO does not seem to be a significant part of the time spent
    */

    if (extendedTests) {

      // (A) full version with all checks

      for (int j=0;j<ctbH;j++)
        for (int i=0;i<ctbW;i++) {

          if ((sps->pcm_loop_filter_disable_flag &&
               img->get_pcm_flag((xC+i)<<chromashift,(yC+j)<<chromashift)) ||
              img->get_cu_transquant_bypass((xC+i)<<chromashift,(yC+j)<<chromashift)) {
            continue;
          }

          int bandIdx = bandTable[ in_img[xC+i+(yC+j)*in_stride]>>bandShift ];

          if (bandIdx>0) {
            int offset = saoinfo->saoOffsetVal[cIdx][bandIdx-1];

            logtrace(LogSAO,"%d %d (%d) offset %d  %x -> %x\n",xC+i,yC+j,bandIdx,
                     offset,
                     in_img[xC+i+(yC+j)*in_stride],
                     in_img[xC+i+(yC+j)*in_stride]+offset);
          
            out_img[xC+i+(yC+j)*out_stride] = Clip3(0,maxPixelValue,
                                                    in_img[xC+i+(yC+j)*in_stride] + offset);
          }
        }
    }
    else
      {
        // (B) simplified version (only works if no PCM and transquant_bypass is active)

        for (int j=0;j<ctbH;j++)
          for (int i=0;i<ctbW;i++) {

            int bandIdx = bandTable[ in_img[xC+i+(yC+j)*in_stride]>>bandShift ];

            if (bandIdx>0) {
              int offset = saoinfo->saoOffsetVal[cIdx][bandIdx-1];

              out_img[xC+i+(yC+j)*out_stride] = Clip3(0,maxPixelValue,
                                                      in_img[xC+i+(yC+j)*in_stride] + offset);
            }
          }
      }
  }
}


void apply_sample_adaptive_offset(de265_image* img)
{
  if (img->sps.sample_adaptive_offset_enabled_flag==0) {
    return;
  }

  de265_image inputCopy;
  de265_error err = inputCopy.copy_image(img);
  if (err != DE265_OK) {
    img->decctx->add_warning(DE265_WARNING_CANNOT_APPLY_SAO_OUT_OF_MEMORY,false);
    return;
  }

  for (int yCtb=0; yCtb<img->sps.PicHeightInCtbsY; yCtb++)
    for (int xCtb=0; xCtb<img->sps.PicWidthInCtbsY; xCtb++)
      {
        const slice_segment_header* shdr = img->get_SliceHeaderCtb(xCtb,yCtb);

        if (shdr->slice_sao_luma_flag) {
          apply_sao(img, xCtb,yCtb, shdr, 0, 1<<img->sps.Log2CtbSizeY,
                    inputCopy.get_image_plane(0), inputCopy.get_image_stride(0),
                    img->get_image_plane(0), img->get_image_stride(0));
        }

        if (shdr->slice_sao_chroma_flag) {
          apply_sao(img, xCtb,yCtb, shdr, 1, 1<<(img->sps.Log2CtbSizeY-1),
                    inputCopy.get_image_plane(1), inputCopy.get_image_stride(1),
                    img->get_image_plane(1), img->get_image_stride(1));

          apply_sao(img, xCtb,yCtb, shdr, 2, 1<<(img->sps.Log2CtbSizeY-1),
                    inputCopy.get_image_plane(2), inputCopy.get_image_stride(2),
                    img->get_image_plane(2), img->get_image_stride(2));
        }
      }
}


void apply_sample_adaptive_offset_sequential(de265_image* img)
{
  if (img->sps.sample_adaptive_offset_enabled_flag==0) {
    return;
  }


  uint8_t* inputCopy = new uint8_t[ img->get_image_stride(0) * img->get_height(0) ];
  if (inputCopy == NULL) {
    img->decctx->add_warning(DE265_WARNING_CANNOT_APPLY_SAO_OUT_OF_MEMORY,false);
    return;
  }


  for (int cIdx=0;cIdx<3;cIdx++) {

    int stride = img->get_image_stride(cIdx);
    int height = img->get_height(cIdx);

    memcpy(inputCopy, img->get_image_plane(cIdx), stride * height);

    for (int yCtb=0; yCtb<img->sps.PicHeightInCtbsY; yCtb++)
      for (int xCtb=0; xCtb<img->sps.PicWidthInCtbsY; xCtb++)
        {
          const slice_segment_header* shdr = img->get_SliceHeaderCtb(xCtb,yCtb);

          if (cIdx==0 && shdr->slice_sao_luma_flag) {
            apply_sao(img, xCtb,yCtb, shdr, 0, 1<<img->sps.Log2CtbSizeY,
                      inputCopy, stride,
                      img->get_image_plane(0), img->get_image_stride(0));
          }

          if (cIdx!=0 && shdr->slice_sao_chroma_flag) {
            apply_sao(img, xCtb,yCtb, shdr, cIdx, 1<<(img->sps.Log2CtbSizeY-1),
                      inputCopy, stride,
                      img->get_image_plane(cIdx), img->get_image_stride(cIdx));
          }
        }
  }

  delete[] inputCopy;
}




class thread_task_sao : public thread_task
{
public:
  int  ctb_y;
  de265_image* inputImg;
  de265_image* outputImg;
  int inputProgress;

  virtual void work();
};


void thread_task_sao::work()
{
  de265_image* img = outputImg;

  state = Running;
  img->thread_run();

  //img->wait_for_progress(this, rightCtb,CtbRow, CTB_PROGRESS_PREFILTER);

  //int ctbSize = img->sps.CtbSizeY;
  //int ctbHeight_luma = ctbSize;
  //int y0_luma = ctb_y * ctbSize;
  int rightCtb = img->sps.PicWidthInCtbsY-1;


  // wait until also the CTB-rows below and above are ready

  if (ctb_y>0) {
    img->wait_for_progress(this, rightCtb,ctb_y-1, inputProgress);
  }
  
  img->wait_for_progress(this, rightCtb,ctb_y,  inputProgress);
  
  if (ctb_y+1<img->sps.PicHeightInCtbsY) {
    img->wait_for_progress(this, rightCtb,ctb_y+1, inputProgress);
  }



  for (int xCtb=0; xCtb<img->sps.PicWidthInCtbsY; xCtb++)
    {
      const slice_segment_header* shdr = img->get_SliceHeaderCtb(xCtb,ctb_y);

      if (shdr->slice_sao_luma_flag) {
        apply_sao(outputImg, xCtb,ctb_y, shdr, 0, 1<<img->sps.Log2CtbSizeY,
                  inputImg ->get_image_plane(0), inputImg ->get_image_stride(0),
                  outputImg->get_image_plane(0), outputImg->get_image_stride(0));
      }

      if (shdr->slice_sao_chroma_flag) {
        apply_sao(outputImg, xCtb,ctb_y, shdr, 1, 1<<(img->sps.Log2CtbSizeY-1),
                  inputImg ->get_image_plane(1), inputImg ->get_image_stride(1),
                  outputImg->get_image_plane(1), outputImg->get_image_stride(1));

        apply_sao(img, xCtb,ctb_y, shdr, 2, 1<<(img->sps.Log2CtbSizeY-1),
                  inputImg ->get_image_plane(2), inputImg ->get_image_stride(2),
                  outputImg->get_image_plane(2), outputImg->get_image_stride(2));
      }
    }

  /*
    for (int x=0;x<=rightCtb;x++) {
    const int CtbWidth = img->sps.PicWidthInCtbsY;
    img->ctb_progress[x+ctb_y*CtbWidth].set_progress(finalProgress);
    }
  */

  state = Finished;
  img->thread_finishes();
}


bool add_sao_tasks(image_unit* imgunit, int saoInputProgress)
{
  de265_image* img = imgunit->img;

  if (img->sps.sample_adaptive_offset_enabled_flag==0) {
    return false;
  }


  decoder_context* ctx = img->decctx;

  img->wait_for_completion(); // currently need barrier because we copy input image

  de265_image inputCopy;
  de265_error err = inputCopy.copy_image(img);
  if (err != DE265_OK) {
    img->decctx->add_warning(DE265_WARNING_CANNOT_APPLY_SAO_OUT_OF_MEMORY,false);
    return false;
  }

  int nRows = img->sps.PicHeightInCtbsY;

  int n=0;
  img->thread_start(nRows);

  for (int y=0;y<img->sps.PicHeightInCtbsY;y++)
    {
      thread_task_sao* task = new thread_task_sao;

      task->inputImg  = &inputCopy;
      task->outputImg = img;
      task->ctb_y = y;
      task->inputProgress = saoInputProgress;

      imgunit->tasks.push_back(task);
      add_task(&ctx->thread_pool, task);
      n++;
    }

  img->wait_for_completion(); // currently need barrier here because 'inputCopy' is local
  return true;
}
