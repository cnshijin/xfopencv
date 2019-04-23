/***************************************************************************
 Copyright (c) 2018, Xilinx, Inc.
 All rights reserved.

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

 3. Neither the name of the copyright holder nor the names of its contributors
 may be used to endorse or promote products derived from this software
 without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 HOWEVER CXFSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***************************************************************************/
#ifndef _XF_MEDIAN_BLUR_
#define _XF_MEDIAN_BLUR_

#include "ap_int.h"
#include "hls_stream.h"
#include "common/xf_common.h"
#include "common/xf_utility.h"

namespace xf{

  template<int PLANES,int NPC,int DEPTH, int WIN_SZ, int WIN_SZ_SQ>
  void xFMedianProc(
                    XF_PTUNAME(DEPTH) OutputValues[XF_NPIXPERCYCLE(NPC)],
                    XF_PTUNAME(DEPTH) src_buf[WIN_SZ][XF_NPIXPERCYCLE(NPC)+(WIN_SZ-1)],
                    ap_uint<8> win_size
                    )
  {
    XF_PTUNAME(DEPTH) out_val;
    XF_PTUNAME(DEPTH) array[WIN_SZ_SQ];
    XF_PTUNAME(DEPTH) array_channel[WIN_SZ_SQ];

    int array_ptr=0;

  Compute_Grad_Loop:
    for(int copy_arr=0; copy_arr<WIN_SZ; copy_arr++)
      {
        for (int copy_in=0; copy_in<WIN_SZ; copy_in++)
          {
            array[array_ptr] = src_buf[copy_arr][copy_in];
            array_ptr++;
          }
      }

    for(int channel=0,k=0; channel<PLANES; channel++,k+=8)
      {
        for(int p=0; p<WIN_SZ_SQ; p++)
          {
            array_channel[p]=array[p].range(k+7,k);
          }

      xFApplyMaskLoop:
        for(int16_t j=0; j<=WIN_SZ_SQ-1; j++)
          {
            int16_t tmp = j & 0x0001;
            if(tmp == 0)
              {
              xFSortLoop1:
                for(int i=0; i <=((WIN_SZ_SQ>>1)-1); i++)    // even sort
                  {
                    int c = (i*2);
                    int c1 = (c + 1);

                    if(array_channel[c] < array_channel[c1])
                      {
                        XF_PTUNAME(DEPTH) temp = array_channel[c];
                        array_channel[c] = array_channel[c1];
                        array_channel[c1] = temp;
                      }
                  }
              }
            else
              {
              xFSortLoop2:
                for(int i=0; i<=((WIN_SZ_SQ>>1 )-1); i++)      // odd sort WINDOW_SIZE_H>>1 -1
                  {
                    int c = (i*2);
                    int c1 = (c + 1);
                    int c2 = (c + 2);
                    if(array_channel[c1] < array_channel[c2])
                      {
                        XF_PTUNAME(DEPTH) temp = array_channel[c1];
                        array_channel[c1] = array_channel[c2];
                        array_channel[c2] = temp;
                      }
                  }
              }
          }

        out_val.range(k+7,k)=array_channel[(WIN_SZ_SQ)>>1];
      }

    OutputValues[0] = out_val;
    return;
  }

  template<int ROWS, int COLS,int PLANES, int TYPE, int NPC, int WORDWIDTH, int TC, int WIN_SZ, int WIN_SZ_SQ>
  void ProcessMedian3x3(
                        xf::Mat<TYPE, ROWS, COLS, NPC> & _src_mat,
                        xf::Mat<TYPE, ROWS, COLS, NPC> & _out_mat,
                        XF_TNAME(TYPE,NPC) buf[WIN_SZ][(COLS >> XF_BITSHIFT(NPC))],          // 行缓存
                        XF_PTUNAME(TYPE)   src_buf[WIN_SZ][XF_NPIXPERCYCLE(NPC)+(WIN_SZ-1)], // 3x3 窗口
                        XF_PTUNAME(TYPE)   OutputValues[XF_NPIXPERCYCLE(NPC)],
                        XF_TNAME(TYPE,NPC) &P0,
                        uint16_t img_width,
                        uint16_t img_height,
                        uint16_t &shift_x,
                        ap_uint<13> row_ind[WIN_SZ], // 指示行缓存的前后行顺序，位置 0 表示最先存储行
                        ap_uint<13> row,
                        ap_uint<8>  win_size,
                        int &rd_ind,
                        int &wr_ind)
  {
    XF_TNAME(TYPE,NPC) buf_cop[WIN_SZ];

    uint16_t npc = XF_NPIXPERCYCLE(NPC);
    uint16_t col_loop_var = 0;

    // col_loop_var 每行需要多处理的时钟个数
    if(npc == 1)
      {
        col_loop_var = (WIN_SZ>>1);
      }
    else
      {
        col_loop_var = 1;
      }

    // 窗口寄存器初始化，npc+WIN_SZ-1 保证 src_buf 在水平方向上有 npc 个 1x3 窗口
    for(int extract_px=0; extract_px<WIN_SZ; extract_px++)
      {
        for(int ext_copy=0; ext_copy<npc+WIN_SZ-1; ext_copy++)
          {
            src_buf[extract_px][ext_copy] = 0;
          }
      }

  Col_Loop:
    for(ap_uint<13> col=0; col<((img_width)>>XF_BITSHIFT(NPC))+col_loop_var; col++)
      {
        if(row<img_height && col<(img_width>>XF_BITSHIFT(NPC))) {
          // row_ind[win_size-1] 代表已被处理的视频行或时间存放最久的视频行 
          buf[row_ind[win_size-1]][col] = _src_mat.data[rd_ind]; // Read data
          rd_ind++;
        }

        if(NPC == XF_NPPC8)
          {
          	// 将行缓存中的数据按写入顺序搬移至 buf_cop 寄存器
            for(int copy_buf_var=0; copy_buf_var<WIN_SZ; copy_buf_var++)
              {
                if((row>(img_height-1)) && (copy_buf_var>(win_size-1-(row-(img_height-1)))))
                  {// 图像底部行边界处理
                    buf_cop[copy_buf_var] = buf[(row_ind[win_size-1-(row-(img_height-1))])][col];
                  }
                else
                  {// 图像右边界处理
                    if(col < (img_width>>XF_BITSHIFT(NPC)))
                      buf_cop[copy_buf_var] = buf[(row_ind[copy_buf_var])][col];
                    else
                      buf_cop[copy_buf_var] = buf_cop[copy_buf_var];
                  }
              }

            XF_PTUNAME(TYPE) src_buf_temp_copy[WIN_SZ][XF_NPIXPERCYCLE(NPC)];
            XF_PTUNAME(TYPE) src_buf_temp_copy_extract[XF_NPIXPERCYCLE(NPC)];

            // 拆解   buf_cop 中的 NPC 并行像素，并存放至 src_buf_temp_copy
            for(int extract_px=0; extract_px<WIN_SZ; extract_px++)
              {
                XF_TNAME(TYPE,NPC) toextract = buf_cop[extract_px];

                // xfExtractPixels，分拆并行的像素
                xfExtractPixels<NPC, XF_WORDWIDTH(TYPE,NPC), XF_DEPTH(TYPE,NPC)>(src_buf_temp_copy_extract, toextract, 0);

                for(int ext_copy=0; ext_copy<npc; ext_copy++)
                  {
                    src_buf_temp_copy[extract_px][ext_copy] = src_buf_temp_copy_extract[ext_copy];
                  }
              }

            // src_buf_temp_copy 移位存放至 src_buf 窗口寄存器
            for(int extract_px=0; extract_px<WIN_SZ; extract_px++)
              {
                for(int col_warp=0; col_warp<(WIN_SZ>>1); col_warp++)
                  {
                    if(col == img_width>>XF_BITSHIFT(NPC))
                      {
                        src_buf[extract_px][col_warp + npc + (WIN_SZ>>1)] = src_buf[extract_px][npc + (WIN_SZ>>1)-1];
                      }
                    else
                      {
                        src_buf[extract_px][col_warp + npc + (WIN_SZ>>1)] = src_buf_temp_copy[extract_px][col_warp];
                      }
                  }
              }

            // 图像左边界处理，复制
            if(col == 0){
              for(int extract_px=0; extract_px<WIN_SZ; extract_px++)
                {
                  for(int col_warp=0; col_warp<npc+(WIN_SZ>>1); col_warp++)
                    {
                      src_buf[extract_px][col_warp] = src_buf_temp_copy[extract_px][0];
                    }
                }
            }

            // 并行处理窗口寄存器 src_buf_temp_med_apply，npc 个 xFMedianProc 处理单元
            XF_PTUNAME(TYPE) src_buf_temp_med_apply[WIN_SZ][XF_NPIXPERCYCLE(NPC)+(WIN_SZ-1)];
            for(int applymedian=0; applymedian<npc; applymedian++)
              {
                for(int copyi=0; copyi<WIN_SZ; copyi++){
                  for(int copyj=0; copyj<WIN_SZ; copyj++){
                    src_buf_temp_med_apply[copyi][copyj] = src_buf[copyi][copyj+applymedian];
                  }
                }
                XF_PTUNAME(TYPE) OutputValues_percycle[XF_NPIXPERCYCLE(NPC)];
                xFMedianProc<PLANES, NPC, TYPE, WIN_SZ, WIN_SZ_SQ>(OutputValues_percycle,src_buf_temp_med_apply, WIN_SZ);
                OutputValues[applymedian] = OutputValues_percycle[0];
              }

            // 输出滤波处理后的数值 P0
            if(col>=1)
              {
                shift_x = 0;
                P0 = 0;
                xfPackPixels<NPC, XF_WORDWIDTH(TYPE,NPC), XF_DEPTH(TYPE,NPC)>(OutputValues, P0, 0, npc, shift_x);
                _out_mat.data[wr_ind] = P0;
                wr_ind++;
              }

            // 窗口寄存器移位更新
            for(int extract_px=0; extract_px<WIN_SZ; extract_px++)
              {
                for(int col_warp=0; col_warp<(WIN_SZ>>1); col_warp++)
                  {
                    src_buf[extract_px][col_warp] = src_buf[extract_px][col_warp + npc];
                  }
              }

            for(int extract_px=0; extract_px<WIN_SZ; extract_px++)
              {
                for(int col_warp=0; col_warp<npc; col_warp++)
                  {
                    src_buf[extract_px][col_warp + (WIN_SZ>>1)] = src_buf_temp_copy[extract_px][col_warp];
                  }
              }

          }
        else
          {
            for(int copy_buf_var=0;copy_buf_var<WIN_SZ;copy_buf_var++)
              {
                if((row >(img_height-1)) && (copy_buf_var>(win_size-1-(row-(img_height-1)))))
                  {
                    buf_cop[copy_buf_var] = buf[(row_ind[win_size-1-(row-(img_height-1))])][col];
                  }
                else
                  {
                    buf_cop[copy_buf_var] = buf[(row_ind[copy_buf_var])][col];
                  }
              }
            for(int extract_px=0;extract_px<WIN_SZ;extract_px++)
              {
                if(col<img_width)
                  {
                    src_buf[extract_px][win_size-1] = buf_cop[extract_px];
                  }
                else
                  {
                    src_buf[extract_px][win_size-1] = src_buf[extract_px][win_size-2];
                  }
              }
            xFMedianProc<PLANES,NPC, TYPE, WIN_SZ, WIN_SZ_SQ>(OutputValues,src_buf, win_size);
            if(col >= (WIN_SZ>>1))
              {
                _out_mat.data[wr_ind] = OutputValues[0];
                wr_ind++;
              }
            for(int wrap_buf=0;wrap_buf<WIN_SZ;wrap_buf++)
              {
                for(int col_warp=0; col_warp<WIN_SZ-1;col_warp++)
                  {
                    if(col==0)
                      {
                        src_buf[wrap_buf][col_warp] = src_buf[wrap_buf][win_size-1];
                      }
                    else
                      {
                        src_buf[wrap_buf][col_warp] = src_buf[wrap_buf][col_warp+1];
                      }
                  }
              }
          }
      } // Col_Loop

  }

  template<int ROWS, int COLS,int PLANES, int TYPE, int NPC, int WORDWIDTH, int TC,int WIN_SZ, int WIN_SZ_SQ>
  void xFMedian3x3(xf::Mat<TYPE, ROWS, COLS, NPC> & _src, xf::Mat<TYPE, ROWS, COLS, NPC> & _dst,
                   ap_uint<8> win_size, uint16_t img_height, uint16_t img_width)
  {
    ap_uint<13> row_ind[WIN_SZ]; // 指示缓存视频行的先后顺序

    uint16_t shift_x = 0;
    ap_uint<13> row, col;
    XF_PTUNAME(TYPE) OutputValues[XF_NPIXPERCYCLE(NPC)];
    XF_PTUNAME(TYPE) src_buf[WIN_SZ][XF_NPIXPERCYCLE(NPC)+(WIN_SZ-1)];
    // src_buf1 et al merged
    XF_TNAME(TYPE,NPC) P0;                                      // 视频 Stream Beat 中间变量
    XF_TNAME(TYPE,NPC) buf[WIN_SZ][(COLS >> XF_BITSHIFT(NPC))]; // 行缓存

    //initializing row index, 如 row_ind[3] = {0, 1, 2}
    for(int init_row_ind=0; init_row_ind<win_size; init_row_ind++)
      {
        row_ind[init_row_ind] = init_row_ind;
      }

    int rd_ind = 0;

    // win_size 等于 3 时，有行缓存 0~2，先填充行缓存 1
    // win_size 等于 5 时，有行缓存 0~4，先填充行缓存 2~3
  read_lines:
    for(int init_buf=row_ind[win_size>>1]; init_buf<row_ind[win_size-1]; init_buf++)
      {
        for(col = 0; col < img_width>>XF_BITSHIFT(NPC); col++)
          {
            buf[init_buf][col] = _src.data[rd_ind];
            rd_ind++;
          }
      }

    // takes care of top borders，复制边界
    for(col = 0; col < img_width>>XF_BITSHIFT(NPC); col++)
      {
        for(int init_buf=0; init_buf<WIN_SZ>>1; init_buf++)
          {
            buf[init_buf][col] = buf[row_ind[win_size>>1]][col];
          }
      }

    int wr_ind = 0;
  Row_Loop:
    for(row = (win_size>>1); row < img_height+(win_size>>1); row++)
      {
        P0 = 0;
        ProcessMedian3x3<ROWS, COLS,PLANES, TYPE, NPC, WORDWIDTH, TC, WIN_SZ, WIN_SZ_SQ>
          (_src, _dst, buf, src_buf, OutputValues, P0, img_width, img_height, shift_x, row_ind, row, win_size, rd_ind, wr_ind);

        //update indices
        ap_uint<13> zero_ind = row_ind[0];
        for(int init_row_ind=0; init_row_ind<WIN_SZ-1; init_row_ind++)
          {
            row_ind[init_row_ind] = row_ind[init_row_ind + 1];
          }
        row_ind[win_size-1] = zero_ind;
      } // Row_Loop
  }

#pragma SDS data mem_attribute("_src.data":NON_CACHEABLE|PHYSICAL_CONTIGUOUS)
#pragma SDS data mem_attribute("_dst.data":NON_CACHEABLE|PHYSICAL_CONTIGUOUS)
#pragma SDS data access_pattern("_src.data":SEQUENTIAL, "_dst.data":SEQUENTIAL)
#pragma SDS data copy("_src.data"[0:"_src.size"], "_dst.data"[0:"_dst.size"])
  template<int FILTER_SIZE, int BORDER_TYPE, int TYPE, int ROWS, int COLS, int NPC=1>
  void medianBlur (xf::Mat<TYPE, ROWS, COLS, NPC> & _src, xf::Mat<TYPE, ROWS, COLS, NPC> & _dst)
  {
#pragma HLS INLINE OFF

    unsigned short imgheight = _src.rows;
    unsigned short imgwidth = _src.cols;

    assert(BORDER_TYPE == XF_BORDER_REPLICATE && "Only XF_BORDER_REPLICATE is supported");

    assert(((imgheight <= ROWS ) && (imgwidth <= COLS)) && "ROWS and COLS should be greater than input image");

    xFMedian3x3<ROWS, COLS,XF_CHANNELS(TYPE,NPC), TYPE, NPC, 0, (COLS>>XF_BITSHIFT(NPC))+(FILTER_SIZE>>1),FILTER_SIZE, FILTER_SIZE*FILTER_SIZE>
      (_src, _dst, FILTER_SIZE, imgheight, imgwidth);

    return;
  }
}
#endif
