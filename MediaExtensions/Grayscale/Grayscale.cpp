// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

#include "Grayscale.h"
#include "bufferlock.h"

#include <math.h>
#include <vector>
#include <algorithm>

#pragma comment(lib, "d2d1")

using namespace Microsoft::WRL;

/*
This code adds sketch effect to video stream by applying edge detection to the Y component of raw video data.   
*/


// Video FOURCC codes.
const DWORD FOURCC_YUY2 = '2YUY'; 
const DWORD FOURCC_UYVY = 'YVYU'; 
const DWORD FOURCC_NV12 = '21VN'; 

// Static array of media types (preferred and accepted).
const GUID g_MediaSubtypes[] =
{
    MFVideoFormat_NV12,
    MFVideoFormat_YUY2,
    MFVideoFormat_UYVY
};

HRESULT GetImageSize(DWORD fcc, UINT32 width, UINT32 height, DWORD* pcbImage);
HRESULT GetDefaultStride(IMFMediaType *pType, LONG *plStride);
bool ValidateRect(const RECT& rc);

template <typename T>
inline T clamp(const T& val, const T& minVal, const T& maxVal)
{
    return (val < minVal ? minVal : (val > maxVal ? maxVal : val));
}


#define abs(x) (x>0) ? (x) : -(x)
#define GRAYTHRESH 48
#define GETPIXELVALUE(val) (255 - ((val+26<255) ? (BYTE)(val+26) : 255))
//#define GETPIXELVALUE(val) (((val+GRAYTHRESH)<255) ? (BYTE)(val+GRAYTHRESH) : 255);

//-------------------------------------------------------------------
// Functions to get median value of input 9 parameters.
//------------------------------------------------------------------

///
///get median value
///
BYTE GetMedian(BYTE _11, BYTE _12, BYTE _13,
			   BYTE _21, BYTE _22, BYTE _23,
			   BYTE _31, BYTE _32, BYTE _33)
{
	DWORD L = 9;
	BYTE data[9] = {_11, _12, _13, 
					_21, _22, _23,
					_31, _32, _33}; 
	/*DWORD L = 5;
	BYTE data[5] = { _12, _21, _22, _23, _32};*/

#if 0
	//bubble
	bool sorted;
	for (DWORD i=0; i<L-1; i++)
	{
		sorted = true;
		for (DWORD j=0; j<L-i-1; j++)
		{
			if (data[j] > data[j+1] )
			{
				BYTE tmp = data[j+1];
				data[j+1] = data[j];
				data[j] = tmp;
				sorted = false;
			}
		}
		if (sorted)
			break;
	}
	return data[L>>1];
#else
	//quick sort
	//int partition(int data[], int p, int r)
	DWORD p, r;
	DWORD M = (L+1)>>1;
	p = 0;
	r = L-1;

	while (true)
	{
		DWORD i,j;
		BYTE pivot = data[p];

		i = p;
		j = r;
		while(i<j)
		{
			while(i<j && data[j]>=pivot) --j;
			data[i] = data[j];
			while(i<j && data[i]<=pivot) ++i;
			data[j] = data[i];
		}
		data[i] = pivot;
		if (i+1 == M)
		{
			return data[i];
		}
		else if(i+1 < M)
		{
			p = i+1;
		}
		else
		{
			r = i-1;
		}
	}
	return data[0];
#endif
}

///
///Using C++ STL lib functions to get median value
///
using std::vector;
inline BYTE GetMedianSTL(	BYTE _11, BYTE _12, BYTE _13,
						BYTE _21, BYTE _22, BYTE _23,
						BYTE _31, BYTE _32, BYTE _33)
{
	vector<BYTE> vec;
	vec.push_back(_11);	vec.push_back(_12);	vec.push_back(_13);
	vec.push_back(_21);	vec.push_back(_22);	vec.push_back(_23);
	vec.push_back(_31);	vec.push_back(_32);	vec.push_back(_33);

	std::sort(vec.begin(), vec.end());
	return vec[vec.size()>>1];
}

//-------------------------------------------------------------------
// Functions to do median filtering on Y component of YUV images.
//
// Three YUV formats which have difference pixel layout in memory
// are supported here:
// YUY2:	Y0 U0 Y1 V1 Y2 U1 ... 
// UYVY:	U0 Y0 V0 Y1 U1 Y2 ...
// NV12:	YYYYYYYY
//			YYYYYYYY
//			YYYYYYYY
//			YYYYYYYY
//			UVUVUVUV
//			UVUVUVUV
//
// The filtering functions take the following parameters:
//
// pDest             Pointer to the destination buffer, sizeof which
//					 is imagewidth*imageheight in bytes.
// pSrc              Pointer to the source buffer.
// lSrcStride        Stride of the source buffer, in bytes.
// lDestStride       Stride of the destination buffer, in bytes.
// dwWidthInPixels   Frame width in pixels.
// dwHeightInPixels  Frame height, in pixels.
//-------------------------------------------------------------------

///
///Median filter for YUY2 image
///
void MedianFilter_YUY2(
    _Inout_updates_(_Inexpressible_(lDestStride * dwHeightInPixels)) BYTE *pDest, 
    _In_reads_(_Inexpressible_(lSrcStride * dwHeightInPixels)) const BYTE* pSrc,
	_In_ LONG lSrcStride, 
	_In_ LONG lDestStride,		//width
    _In_ DWORD dwWidthInPixels, 
    _In_ DWORD dwHeightInPixels)
{
	DWORD y = 0;
	DWORD pVal = 0;

	//1st line
	for (DWORD x=0; x<dwWidthInPixels; x++)
	{
		pDest[x] = pSrc[x<<1];
	}
	pSrc	+= lSrcStride;
	pDest	+= lDestStride;

    for ( y=1; y < dwHeightInPixels-1; y++)
    {
        BYTE *pSrc_Pixel = (BYTE*)pSrc;
		BYTE *pDest_Pixel= (BYTE*)pDest;
		DWORD x = 2, p = 1;

		//1st column
		pDest_Pixel[0] = pSrc_Pixel[0];
		
		//Columns from the first to the last 
		for ( ; p<dwWidthInPixels-1; x += 2, p++)
        {
			pDest_Pixel[p] = GetMedian(	pSrc_Pixel[x-2-lSrcStride], pSrc_Pixel[x-lSrcStride], pSrc_Pixel[x+2-lSrcStride],
										pSrc_Pixel[x-2],			pSrc_Pixel[x],				pSrc_Pixel[x+2],
										pSrc_Pixel[x-2+lSrcStride],	pSrc_Pixel[x+lSrcStride],	pSrc_Pixel[x+2+lSrcStride]);
        }

		//Last column
		pDest_Pixel[p] = pSrc_Pixel[x];

        pDest += lDestStride;
        pSrc += lSrcStride;
	}

	//Last line
	for (DWORD x=0; x<dwWidthInPixels; x++)
	{
		pDest[x] = pSrc[x<<1];
	}
}


///
///Meidan filter for UYVY image.
///
void MedianFilter_UYVY(
	_Inout_updates_(_Inexpressible_(lDestStride * dwHeightInPixels)) BYTE *pDest, 
    _In_reads_(_Inexpressible_(lSrcStride * dwHeightInPixels)) const BYTE* pSrc,
	_In_ LONG lSrcStride, 
	_In_ LONG lDestStride,		
    _In_ DWORD dwWidthInPixels, 
    _In_ DWORD dwHeightInPixels)
{
	DWORD y = 0, pVal = 0;

	//1st line
	for (DWORD x=0; x<dwWidthInPixels; x++)
	{
		pDest[x] = pSrc[(x<<1)+1];
	}
	pSrc	+= lSrcStride;
	pDest	+= lDestStride;

    for ( y=1; y < dwHeightInPixels-1; y++)
    {
        BYTE *pSrc_Pixel = (BYTE*)pSrc;
		BYTE *pDest_Pixel= (BYTE*)pDest;
		DWORD x = 3, p = 1;

		//1st column
		pDest_Pixel[0] = pSrc_Pixel[1];
		
		//Columns from the first to the last 
		for ( ; p<dwWidthInPixels-1; x += 2, p++)
        {
			pDest_Pixel[p] = GetMedian(	pSrc_Pixel[x-2-lSrcStride], pSrc_Pixel[x-lSrcStride], pSrc_Pixel[x+2-lSrcStride],
										pSrc_Pixel[x-2],			pSrc_Pixel[x],				pSrc_Pixel[x+2],
										pSrc_Pixel[x-2+lSrcStride],	pSrc_Pixel[x+lSrcStride],	pSrc_Pixel[x+2+lSrcStride]);
        }

		//Last column
		pDest_Pixel[p] = pSrc_Pixel[x];

        pDest += lDestStride;
        pSrc += lSrcStride;
	}

	//Last line
	for (DWORD x=0; x<dwWidthInPixels; x++)
	{
		pDest[x] = pSrc[(x<<1)+1];
	}
}

///
///Median filter for NV12 image
///
void MedianFilter_NV12(
	_Inout_updates_(_Inexpressible_(lDestStride * dwHeightInPixels)) BYTE *pDest, 
    _In_reads_(_Inexpressible_(lSrcStride * dwHeightInPixels)) const BYTE* pSrc,
	_In_ LONG lSrcStride, 
	_In_ LONG lDestStride,		
    _In_ DWORD dwWidthInPixels, 
    _In_ DWORD dwHeightInPixels)
{
	DWORD y = 0, pVal = 0;

	//1st line
	memcpy(pDest, pSrc, lDestStride*sizeof(BYTE));
	pSrc	+= lSrcStride;
	pDest	+= lDestStride;

	for (y=1; y<dwHeightInPixels-1; y++)
	{
		BYTE *pSrc_Pixel	= (BYTE*)pSrc;
		BYTE *pDest_Pixel	= (BYTE*)pDest;
		DWORD x;

		//The 1st column
		pDest_Pixel[0] = pSrc_Pixel[0];

		for (x=1; x<dwWidthInPixels-1; x++)
		{
			pDest_Pixel[x] = GetMedian( pSrc_Pixel[x-1-lSrcStride], pSrc_Pixel[x-lSrcStride],	pSrc_Pixel[x+1-lSrcStride],
										pSrc_Pixel[x-1],			pSrc_Pixel[x],				pSrc_Pixel[x+1],
										pSrc_Pixel[x-1+lSrcStride], pSrc_Pixel[x+lSrcStride],	pSrc_Pixel[x+1+lSrcStride]);
		}

		//last column
		pDest_Pixel[x] = pSrc_Pixel[x];

		pDest	+= lDestStride;
		pSrc	+= lSrcStride;
	}

	//last line
	memcpy(pDest, pSrc, lDestStride*sizeof(BYTE));
}


//-------------------------------------------------------------------
// Functions to do image detection, which take the following parameters:
//
// mat               Transfomation matrix for chroma values.
// rcDest            Destination rectangle.
// pDest             Pointer to the destination buffer.
// lDestStride       Stride of the destination buffer, in bytes.
// pSrc              Pointer to the source buffer.
// lSrcStride        Stride of the source buffer, in bytes.
// dwWidthInPixels   Frame width in pixels.
// dwHeightInPixels  Frame height, in pixels.
//-------------------------------------------------------------------

///
///Edge detection for YUY2 image
///
void EdgeDectection_YUY2(
const D2D1::Matrix3x2F& mat,
const D2D_RECT_U& rcDest,
_Inout_updates_(_Inexpressible_(lDestStride * dwHeightInPixels)) BYTE *pDest, 
_In_ LONG lDestStride, 
_In_reads_(_Inexpressible_(lSrcStride * dwHeightInPixels)) const BYTE* pSrc,
_In_ LONG lSrcStride, 
_In_ DWORD dwWidthInPixels, 
_In_ DWORD dwHeightInPixels,
_In_ BYTE *pFilteredYSrc)
{
	DWORD y = 0;
    const DWORD y0 = min(rcDest.bottom, dwHeightInPixels);
	DWORD pVal = 0;

    // Lines above the destination rectangle and the first line in the dest. Rec.
    for ( ; y < rcDest.top + 1; y++)
    {
        memcpy(pDest, pSrc, dwWidthInPixels * 2);
        pSrc += lSrcStride;
        pDest += lDestStride;
    }

	// Lines from the first to the last line [1, y0)
    for ( y=1; y < y0-1; y++)
    {
        BYTE *pSrc_Pixel = (BYTE*)pSrc;
        BYTE *pDest_Pixel = (BYTE*)pDest;

		//Pixel in the fist column
		pDest_Pixel[0] = pSrc_Pixel[0];
		pDest_Pixel[1] = 128;	//u

		//Columns from the first to the last 
		for (DWORD x = 2, p = 0; (LONG)x < lSrcStride-2; x += 2, p++)
        {
			DWORD dwVal = 0;

			//Roberts detector.
			//	P1	p2
			//	P3	P4
			//  P1 = |p1-p4|+|p2-p3|
			pVal	=	abs(*(pSrc_Pixel+x)-*(pSrc_Pixel+lSrcStride+x+2)) + abs(*(pSrc_Pixel+x+2)-*(pSrc_Pixel+lSrcStride+x));
			
			//Laplician detector
			//	1	1	1
			//	1	-8	1
			//	1	1	1
			/*SHORT tmp = 0;
			tmp =	*(pSrc_Pixel+x-2) + *(pSrc_Pixel+x+2);
			tmp	+=	*(pSrc_Pixel+x-2 - lSrcStride) + *(pSrc_Pixel+x - lSrcStride) + *(pSrc_Pixel+x+2 - lSrcStride);
			tmp +=	*(pSrc_Pixel+x-2 + lSrcStride) + *(pSrc_Pixel+x + lSrcStride) + *(pSrc_Pixel+x+2 + lSrcStride);
			tmp -= pSrc_Pixel[x]*8;
			pVal = abs(tmp);*/
			
			//
			//pDest_Pixel[x] = (pVal>30) ? 0 : 210;							//threshhold way
			//pDest_Pixel[x] = 255 - ((pVal+32<255) ? pVal+32 : 255);		//white bck.
			//pDest_Pixel[x] = (pVal+64<255) ? pVal+64 : 255;

			//
			dwVal = pVal*pVal;	
			pDest_Pixel[x] = GETPIXELVALUE(dwVal);

			//to U/V Comp.
			pDest_Pixel[x+1] = 128;	
        }


		//Pixel in the last column
		pDest_Pixel[lDestStride-2] = pSrc_Pixel[lSrcStride-2];
		pDest_Pixel[lDestStride-1] = 128; //v

        pDest += lDestStride;
        pSrc += lSrcStride;
    }

    //The last line in the dest. rect. 
	memcpy(pDest, pSrc, dwWidthInPixels * 2);
	for (DWORD x=0; x<dwWidthInPixels; x++)
	{
		pDest[(x<<1)+1] = 128;		//u, v
	}
}

//
// Edge detection with filter for YUY2 image
//
void EdgeDectectionF_YUY2(
const D2D1::Matrix3x2F& mat,
const D2D_RECT_U& rcDest,
_Inout_updates_(_Inexpressible_(lDestStride * dwHeightInPixels)) BYTE *pDest, 
_In_ LONG lDestStride, 
_In_reads_(_Inexpressible_(lSrcStride * dwHeightInPixels)) const BYTE* pSrc,
_In_ LONG lSrcStride, 
_In_ DWORD dwWidthInPixels, 
_In_ DWORD dwHeightInPixels,
_In_ BYTE* pFilteredYSrc)
{
	DWORD y = 0;
    const DWORD y0 = min(rcDest.bottom, dwHeightInPixels);
	DWORD pVal = 0;

	//Apply median filter to Y comp.
	MedianFilter_YUY2(pFilteredYSrc, pSrc,lSrcStride, dwWidthInPixels, dwWidthInPixels, dwHeightInPixels);

	const BYTE *pSrcFiltered = pFilteredYSrc;

    // Lines above the destination rectangle and the first line in the dest. Rec.
    for ( ; y < rcDest.top + 1; y++)
    {
        memcpy(pDest, pSrc, dwWidthInPixels * 2);
        pSrc += lSrcStride;
        pDest += lDestStride;
		pSrcFiltered += dwWidthInPixels;
    }

	// Lines from the first to the last line [1, y0)
	for ( y=1; y < dwHeightInPixels-1; y++)
    {
        BYTE *pSrc_Pixel = (BYTE*)pSrcFiltered;
        BYTE *pDest_Pixel = (BYTE*)pDest;

		//Pixel in the fist column
		pDest_Pixel[0] = pSrc_Pixel[0];
		pDest_Pixel[1] = 128;	//u

		//Columns from the first to the last 
		for (DWORD x = 2, p = 1; (LONG)x < lSrcStride-2; x += 2, p++)
        {
			DWORD dwVal = 0;

			//Roberts detector.
			//	P1	p2
			//	P3	P4
			//  P1 = |p1-p4|+|p2-p3|
			pVal	=	abs(*(pSrc_Pixel+p)-*(pSrc_Pixel+dwWidthInPixels+p+1)) + abs(*(pSrc_Pixel+p+1)-*(pSrc_Pixel+lSrcStride+p));

			//
			dwVal = pVal*pVal;	
			pDest_Pixel[x] = GETPIXELVALUE(dwVal);

			//to U/V Comp.
			pDest_Pixel[x+1] = 128;	
        }

		//Pixel in the last column
		pDest_Pixel[lDestStride-2] = pSrc_Pixel[dwWidthInPixels-1];
		pDest_Pixel[lDestStride-1] = 128; //v

        pDest	+= lDestStride;
		pSrc	+= lSrcStride;
		pSrcFiltered += dwWidthInPixels;
    }

    //The last line in the dest. rect. 
	memcpy(pDest, pSrc, dwWidthInPixels * 2);
	for (DWORD x=0; x<dwWidthInPixels; x++)
	{
		pDest[(x<<1)+1] = 128;		//u, v
	}
}

///
///Edge detection for UYVY image
///
void EdgeDectection_UYVY(
const D2D1::Matrix3x2F& mat,
const D2D_RECT_U& rcDest,
_Inout_updates_(_Inexpressible_(lDestStride * dwHeightInPixels)) BYTE *pDest, 
_In_ LONG lDestStride, 
_In_reads_(_Inexpressible_(lSrcStride * dwHeightInPixels)) const BYTE* pSrc,
_In_ LONG lSrcStride, 
_In_ DWORD dwWidthInPixels, 
_In_ DWORD dwHeightInPixels,
_In_ BYTE *pFilteredYSrc)
{
	DWORD y = 0;
    const DWORD y0 = min(rcDest.bottom, dwHeightInPixels);
	DWORD pVal = 0;

    // Lines above the destination rectangle and the first line in the dest. Rec.
    for ( ; y < rcDest.top + 1; y++)
    {
        memcpy(pDest, pSrc, dwWidthInPixels * 2);
        pSrc += lSrcStride;
        pDest += lDestStride;
    }

	// Lines between the first and the last line [1, y0)
    for ( y=1; y < y0-1; y++)
    {
        BYTE *pSrc_Pixel = (BYTE*)pSrc;
        BYTE *pDest_Pixel = (BYTE*)pDest;

		//Pixel in the fist column
		pDest_Pixel[0] = 128;	//u
		pDest_Pixel[1] = pSrc_Pixel[1];

		//Columns from the first to the last 
		for (DWORD x = 3, p = 0; (LONG)x < lSrcStride-1; x += 2, p++)
        {
			DWORD dwVal = 0;

			//Roberts detector.
			//	P1	p2
			//	P3	P4
			//  P1 = |p1-p4|+|p2-p3|
			pVal	=	abs(*(pSrc_Pixel+x)-*(pSrc_Pixel+lSrcStride+x+2)) + abs(*(pSrc_Pixel+x+2)-*(pSrc_Pixel+lSrcStride+x));

			//
			dwVal = pVal*pVal;	
			pDest_Pixel[x] = GETPIXELVALUE(dwVal);

			//to U/V Comp.
			pDest_Pixel[x-1] = 128;	
        }


		//Pixel in the last column
		pDest_Pixel[lDestStride-2] = 128; //v
		pDest_Pixel[lDestStride-1] = pSrc_Pixel[lSrcStride-1];

        pDest += lDestStride;
        pSrc += lSrcStride;
    }

    //The last line in the dest. rect. 
	memcpy(pDest, pSrc, dwWidthInPixels * 2);
	for (DWORD x=0; x<dwWidthInPixels; x++)
	{
		pDest[x<<1] = 128;		//u, v
	}
}

///
/// Ede detection with filtr for UYVY image
///
void EdgeDectectionF_UYVY(
const D2D1::Matrix3x2F& mat,
const D2D_RECT_U& rcDest,
_Inout_updates_(_Inexpressible_(lDestStride * dwHeightInPixels)) BYTE *pDest, 
_In_ LONG lDestStride, 
_In_reads_(_Inexpressible_(lSrcStride * dwHeightInPixels)) const BYTE* pSrc,
_In_ LONG lSrcStride, 
_In_ DWORD dwWidthInPixels, 
_In_ DWORD dwHeightInPixels,
_In_ BYTE* pFilteredYSrc)
{
	DWORD y = 0;
    const DWORD y0 = min(rcDest.bottom, dwHeightInPixels);
	DWORD pVal = 0;

	//Apply median filter to Y comp.
	MedianFilter_UYVY(pFilteredYSrc, pSrc,lSrcStride, dwWidthInPixels, dwWidthInPixels, dwHeightInPixels);

	const BYTE *pSrcFiltered = pFilteredYSrc;

    // Lines above the destination rectangle and the first line (line 0) in the dest. Rec.
    for ( ; y < rcDest.top + 1; y++)
    {
        memcpy(pDest, pSrc, dwWidthInPixels * 2);
        pSrc += lSrcStride;
        pDest += lDestStride;
		pSrcFiltered += dwWidthInPixels;
    }

	// Lines from the first to the last line [1, y0)
    for ( y=1; y < y0-1; y++)
    {
        BYTE *pSrc_Pixel = (BYTE*)pSrcFiltered;
        BYTE *pDest_Pixel = (BYTE*)pDest;

		//Pixel in the first column
		pDest_Pixel[0] = 128;	//U
		pDest_Pixel[1] = pSrc_Pixel[1];

		//Columns from the first to the last 
		for (DWORD x = 3, p = 1; p < dwWidthInPixels-1; x += 2, p++)
        {
			DWORD dwVal = 0;

			//Roberts detector.
			//	P1	p2
			//	P3	P4
			//  P1 = |p1-p4|+|p2-p3|
			pVal	=	abs(*(pSrc_Pixel+p)-*(pSrc_Pixel+dwWidthInPixels+p+1)) + abs(*(pSrc_Pixel+p+1)-*(pSrc_Pixel+lSrcStride+p));

			//
			dwVal = pVal*pVal;	
			pDest_Pixel[x] = GETPIXELVALUE(dwVal);

			//to U/V Comp.
			pDest_Pixel[x-1] = 128;	
        }

		//Pixel in the last column
		pDest_Pixel[lDestStride-1] = pSrc_Pixel[dwWidthInPixels-1];
		pDest_Pixel[lDestStride-2] = 128; //v

        pDest	+= lDestStride;
		pSrc	+= lSrcStride;
		pSrcFiltered += dwWidthInPixels;
    }

     //The last line in the dest. rect. 
	memcpy(pDest, pSrc, dwWidthInPixels * 2);
	for (DWORD x=0; x<dwWidthInPixels; x++)
	{
		pDest[x<<1] = 128;		//u, v
	}
}

///
///Edge detection for NV12 image
///
void EdgeDectection_NV12(
const D2D1::Matrix3x2F& mat,
const D2D_RECT_U& rcDest,
_Inout_updates_(_Inexpressible_(lDestStride * dwHeightInPixels)) BYTE *pDest, 
_In_ LONG lDestStride, 
_In_reads_(_Inexpressible_(lSrcStride * dwHeightInPixels)) const BYTE* pSrc,
_In_ LONG lSrcStride, 
_In_ DWORD dwWidthInPixels, 
_In_ DWORD dwHeightInPixels,
_In_ BYTE *pFilteredYSrc)
{
	DWORD y = 0;
    const DWORD y0 = min(rcDest.bottom, dwHeightInPixels);
	DWORD pVal = 0;

	//-----------------------------------------------------------------------------------------//
	// Y component
	//-----------------------------------------------------------------------------------------//

    // Lines above the destination rectangle and the first line in the dest. Rec.
    for ( ; y < rcDest.top + 1; y++)
    {
        memcpy(pDest, pSrc, dwWidthInPixels);
        pSrc += lSrcStride;
        pDest += lDestStride;
    }

	// Lines between the first and the last line [1, y0)
    for ( y=1; y < y0-1; y++)
    {
        BYTE *pSrc_Pixel = (BYTE*)pSrc;
        BYTE *pDest_Pixel = (BYTE*)pDest;
		DWORD x;

		//Pixel in the fist column
		pDest_Pixel[0] = pSrc_Pixel[0];

		//Columns from the first to the last 
		for (x = 1; (LONG)x < lSrcStride-1; x ++)
        {
			DWORD dwVal = 0;

			//Roberts detector.
			//	P1	p2
			//	P3	P4
			//  P1 = |p1-p4|+|p2-p3|
			pVal	=	abs(*(pSrc_Pixel+x)-*(pSrc_Pixel+lSrcStride+x+1)) + abs(*(pSrc_Pixel+x+1)-*(pSrc_Pixel+lSrcStride+x));

			//
			dwVal = pVal*pVal;	
			pDest_Pixel[x] = GETPIXELVALUE(dwVal);
        }


		//Pixel in the last column
		pDest_Pixel[lDestStride-1] = pSrc_Pixel[lSrcStride-1];

        pDest += lDestStride;
        pSrc += lSrcStride;
    }

    //The last line in the dest. rect. 
	memcpy(pDest, pSrc, dwWidthInPixels);
	pDest	+= lDestStride;

	//-----------------------------------------------------------------------------------------//
	// U/V component
	//-----------------------------------------------------------------------------------------//
	memset(pDest, 128, (dwHeightInPixels>>1)*dwWidthInPixels);
}

///
/// Edde detection with filter for NV12 image
///
void EdgeDectectionF_NV12(
const D2D1::Matrix3x2F& mat,
const D2D_RECT_U& rcDest,
_Inout_updates_(_Inexpressible_(lDestStride * dwHeightInPixels)) BYTE *pDest, 
_In_ LONG lDestStride, 
_In_reads_(_Inexpressible_(lSrcStride * dwHeightInPixels)) const BYTE* pSrc,
_In_ LONG lSrcStride, 
_In_ DWORD dwWidthInPixels, 
_In_ DWORD dwHeightInPixels,
_In_ BYTE* pFilteredYSrc)
{
	DWORD y = 0;
    const DWORD y0 = min(rcDest.bottom, dwHeightInPixels);
	DWORD pVal = 0;

	// Apply median filter to Y comp.
	MedianFilter_NV12(pFilteredYSrc, pSrc,lSrcStride, dwWidthInPixels, dwWidthInPixels, dwHeightInPixels);

	const BYTE *pSrcFiltered = pFilteredYSrc;

	//-----------------------------------------------------------------------------------------//
	// Y component
	//-----------------------------------------------------------------------------------------//
	// The first line
	memcpy(pDest, pSrc, lDestStride);
	pSrc	+= lSrcStride;
	pDest	+= lDestStride;

	// Lines between the 2nd and the last line
	for ( y=1; y<dwHeightInPixels-1; y++)
	{
		BYTE *pSrc_Pixel = (BYTE*)pSrcFiltered;
        BYTE *pDest_Pixel = (BYTE*)pDest;
		DWORD x;

		// Pixel in the first column
		pDest_Pixel[0] = pSrc_Pixel[0];

		//Columns between the first and the last column
		for (x = 1; x < dwWidthInPixels-1; x ++)
        {
			DWORD dwVal = 0;

			//Roberts detector.
			//	P1	p2
			//	P3	P4
			//  P1 = |p1-p4|+|p2-p3|
			pVal	=	abs(*(pSrc_Pixel+x)-*(pSrc_Pixel+dwWidthInPixels+x+1)) + abs(*(pSrc_Pixel+x+1)-*(pSrc_Pixel+lSrcStride+x));

			//
			dwVal = pVal*pVal;	
			pDest_Pixel[x] = GETPIXELVALUE(dwVal);
        }

		//The last column
		pDest_Pixel[x] = pSrc_Pixel[x];

		pSrc	+= lSrcStride;
		pDest	+= lDestStride;
		pSrcFiltered += dwWidthInPixels;
	}

	//The last line
	memcpy(pDest, pSrc, dwWidthInPixels);
	pDest	+= lDestStride;

	
	//-----------------------------------------------------------------------------------------//
	// U/V component
	//-----------------------------------------------------------------------------------------//
	memset(pDest, 128, (dwHeightInPixels>>1)*dwWidthInPixels);
}


///
///Convert YUY2 data to RGB pixels
///
void YUY2toRGB(
    _Inout_updates_(_Inexpressible_(lDestStride * dwHeightInPixels)) BYTE *pDest, 
    _In_reads_(_Inexpressible_(lSrcStride * dwHeightInPixels)) const BYTE* pSrc,
	_In_ LONG lSrcStride, 
	_In_ LONG lDestStride,		//width
    _In_ DWORD dwWidthInPixels, 
    _In_ DWORD dwHeightInPixels)
{
	DWORD y = 0;
	DWORD pVal = 0;

    for ( y=0; y < dwHeightInPixels; y++)
    {
        BYTE *pSrc_Pixel = (BYTE*)pSrc;
        BYTE *pR = (BYTE*)pDest;
		BYTE *pG = pR + dwWidthInPixels*dwHeightInPixels;
		BYTE *pB = pG + dwWidthInPixels*dwHeightInPixels;

		//Columns from the first to the last 
		for (DWORD x = 0, p = 0; p<dwWidthInPixels; x += 2, p++)
        {
			SHORT rdif=0, invdif=0, bdif=0;
			/*float a = 0;
			float b = 0;
			float c = 0;
			if (p%2==0)
			{
				SHORT U, V;
				U = pSrc_Pixel[x+1] - 128;
				V = pSrc_Pixel[x+3] - 128;
				a = 1.5*V;
				b = 0.35*U + 0.7*V;
				c = 1.8*U;
			}
			pR[p] = (pSrc_Pixel[x] + a)>255 ? 255 : (BYTE)(pSrc_Pixel[x] + a);
			pG[p] = (pSrc_Pixel[x] - b)>255 ? 255 : (BYTE)(pSrc_Pixel[x] - b);
			pB[p] = (pSrc_Pixel[x] + c)>255 ? 255 : (BYTE)(pSrc_Pixel[x] + c);*/
			if (p%2 == 0)
			{
				SHORT U, V;
				U = pSrc_Pixel[x+1] - 128;
				V = pSrc_Pixel[x+3] - 128;
				rdif	 = V + ((V*103)>>8);
				invdif	 = ((U*88)>>8) + ((V*183)>>8);
				bdif	 = U + ((U*198>>8));
			}			
			pVal	= (pSrc_Pixel[x] + rdif);
			pR[p]	= pVal > 255 ? 255 : (BYTE)pVal;
			pVal	= pSrc_Pixel[x] - invdif;
			pG[p]	= pVal > 255 ? 255 : (BYTE)pVal;
			pVal	= pSrc_Pixel[x] + bdif;
			pB[p]	= pVal > 255 ? 255 : (BYTE)pVal;
        }

        pDest += lDestStride;
        pSrc += lSrcStride;
    }

}

///
///Do edge detection on RGB data
///
void EdgeDectection_YUY2RGB(
    const D2D1::Matrix3x2F& mat,
    const D2D_RECT_U& rcDest,
    _Inout_updates_(_Inexpressible_(lDestStride * dwHeightInPixels)) BYTE *pDest, 
    _In_ LONG lDestStride, 
    _In_reads_(_Inexpressible_(lSrcStride * dwHeightInPixels)) const BYTE* pSrc,
    _In_ LONG lSrcStride, 
    _In_ DWORD dwWidthInPixels, 
    _In_ DWORD dwHeightInPixels)
{
	DWORD y = 0;
    const DWORD y0 = min(rcDest.bottom, dwHeightInPixels);
	DWORD pVal = 0;

	BYTE *rgbBuf, *pR;
	rgbBuf = (BYTE*)calloc(dwHeightInPixels*dwWidthInPixels*3, sizeof(BYTE));

	//convert yuv to rgb
	YUY2toRGB(rgbBuf, pSrc,lSrcStride, dwWidthInPixels, dwWidthInPixels, dwHeightInPixels);

    // Lines above the destination rectangle and the first line in the dest. Rec.
    for ( ; y < rcDest.top + 1; y++)
    {
        memcpy(pDest, pSrc, dwWidthInPixels * 2);
        pSrc += lSrcStride;
        pDest += lDestStride;
    }

	pR = rgbBuf;
	pR += dwWidthInPixels;

	// Lines from the first to the last line [1, y0)
    for ( y=1; y < y0-1; y++)
    {
        BYTE *pSrc_Pixel = (BYTE*)pSrc;
        BYTE *pDest_Pixel = (BYTE*)pDest;
		BYTE *pG = pR + dwWidthInPixels*dwHeightInPixels;
		BYTE *pB = pG + dwWidthInPixels*dwHeightInPixels;

		//Pixel in the fist column
		pDest_Pixel[0] = pSrc_Pixel[0];
		pDest_Pixel[1] = 128;	//u

		//Columns from the first to the last 
		for (DWORD x = 2, p = 1; (LONG)x < lSrcStride-2; x += 2, p++)
        {
			//Apply roberts detector.
			//	P1	p2
			//	P3	P4
			//  P1 = |p1-p4|+|p2-p3|
			pVal =	abs(pR[p]-pR[p+dwWidthInPixels+1])+abs(pR[p+1]-pR[p+dwWidthInPixels]);
			pVal +=	abs(pG[p]-pG[p+dwWidthInPixels+1])+abs(pG[p+1]-pG[p+dwWidthInPixels]);
			pVal += abs(pB[p]-pB[p+dwWidthInPixels+1])+abs(pB[p+1]-pB[p+dwWidthInPixels]);
			
			//Laplician detector
			//	1	1	1
			//	1	-8	1
			//	1	1	1
			/*SHORT tmp = 0;
			tmp =	pR[p-1] + pR[p+1];
			tmp +=  pR[p-1-dwWidthInPixels] + pR[p-dwWidthInPixels] + pR[p-dwWidthInPixels+1];
			tmp +=  pR[p-1+dwWidthInPixels] + pR[p+dwWidthInPixels] + pR[p+dwWidthInPixels+1];
			tmp -=  8*pR[p];
			pVal = abs(tmp);
			tmp =	pG[p-1] + pG[p+1];
			tmp +=  pG[p-1-dwWidthInPixels] + pG[p-dwWidthInPixels] + pG[p-dwWidthInPixels+1];
			tmp +=  pG[p-1+dwWidthInPixels] + pG[p+dwWidthInPixels] + pG[p+dwWidthInPixels+1];
			tmp -=  8*pG[p];
			pVal  += abs(tmp);
			tmp =	pB[p-1] + pB[p+1];
			tmp +=  pB[p-1-dwWidthInPixels] + pB[p-dwWidthInPixels] + pB[p-dwWidthInPixels+1];
			tmp +=  pB[p-1+dwWidthInPixels] + pB[p+dwWidthInPixels] + pB[p+dwWidthInPixels+1];
			tmp -=  8*pB[p];
			pVal += abs(tmp);*/

			/*float ftmp = 0.3*pR[p] + 0.6*pG[p] + 0.1*pB[p];
			pDest_Pixel[x] = ((BYTE)ftmp)<255 ? (BYTE)ftmp : 255;*/
			
			//pDest_Pixel[x] = (pVal>35) ? 0 : 255;					//threshhold way
			//pDest_Pixel[x] = 255 - ((pVal<255) ? pVal : 255);		//white bck.
			pDest_Pixel[x] = (pVal<255) ? (BYTE)pVal : 255;

			//to U/V Comp.
			pDest_Pixel[x+1] = 128;	
        }


		//Pixel in the last column
		pDest_Pixel[lDestStride-2] = pSrc_Pixel[lSrcStride-2];
		pDest_Pixel[lDestStride-1] = 128; //v

        pDest += lDestStride;
        pSrc += lSrcStride;
		pR += dwWidthInPixels;
    }

    // Lines below the destination rectangle. and the last line in the dest. rect. 
    for ( ; y < dwHeightInPixels; y++)
    {
        memcpy(pDest, pSrc, dwWidthInPixels * 2);
        pSrc += lSrcStride;
        pDest += lDestStride;
    }

	if (rgbBuf)
	{
		free(rgbBuf);
	}
}

CGrayscale::CGrayscale() :
    m_pSample(NULL), m_pInputType(NULL), m_pOutputType(NULL), m_pTransformFn(NULL),m_pFilteredYSrc(NULL),
    m_imageWidthInPixels(0), m_imageHeightInPixels(0), m_cbImageSize(0),
    m_transform(D2D1::Matrix3x2F::Identity()), m_rcDest(D2D1::RectU()), m_bStreamingInitialized(false),
	m_pAttributes(NULL), m_bBlackFigure(FALSE)
{
    InitializeCriticalSectionEx(&m_critSec, 3000, 0);
}

CGrayscale::~CGrayscale()
{
    SafeRelease(&m_pInputType);
    SafeRelease(&m_pOutputType);
    SafeRelease(&m_pSample);
    SafeRelease(&m_pAttributes);

	if (m_pFilteredYSrc)
	{
		free(m_pFilteredYSrc);
	}
    DeleteCriticalSection(&m_critSec);
}

// Initialize the instance.
STDMETHODIMP CGrayscale::RuntimeClassInitialize()
{
    // Create the attribute store.
    return MFCreateAttributes(&m_pAttributes, 3);
}

// IMediaExtension methods

//-------------------------------------------------------------------
// SetProperties
// Sets the configuration of the effect
//-------------------------------------------------------------------
HRESULT CGrayscale::SetProperties(ABI::Windows::Foundation::Collections::IPropertySet *pConfiguration)
{
    return S_OK;
}

// IMFTransform methods. Refer to the Media Foundation SDK documentation for details.

//-------------------------------------------------------------------
// GetStreamLimits
// Returns the minimum and maximum number of streams.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetStreamLimits(
    DWORD   *pdwInputMinimum,
    DWORD   *pdwInputMaximum,
    DWORD   *pdwOutputMinimum,
    DWORD   *pdwOutputMaximum
)
{
    if ((pdwInputMinimum == NULL) ||
        (pdwInputMaximum == NULL) ||
        (pdwOutputMinimum == NULL) ||
        (pdwOutputMaximum == NULL))
    {
        return E_POINTER;
    }

    // This MFT has a fixed number of streams.
    *pdwInputMinimum = 1;
    *pdwInputMaximum = 1;
    *pdwOutputMinimum = 1;
    *pdwOutputMaximum = 1;
    return S_OK;
}


//-------------------------------------------------------------------
// GetStreamCount
// Returns the actual number of streams.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetStreamCount(
    DWORD   *pcInputStreams,
    DWORD   *pcOutputStreams
)
{
    if ((pcInputStreams == NULL) || (pcOutputStreams == NULL))

    {
        return E_POINTER;
    }

    // This MFT has a fixed number of streams.
    *pcInputStreams = 1;
    *pcOutputStreams = 1;
    return S_OK;
}



//-------------------------------------------------------------------
// GetStreamIDs
// Returns stream IDs for the input and output streams.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetStreamIDs(
    DWORD   dwInputIDArraySize,
    DWORD   *pdwInputIDs,
    DWORD   dwOutputIDArraySize,
    DWORD   *pdwOutputIDs
)
{
    // It is not required to implement this method if the MFT has a fixed number of
    // streams AND the stream IDs are numbered sequentially from zero (that is, the
    // stream IDs match the stream indexes).

    // In that case, it is OK to return E_NOTIMPL.
    return E_NOTIMPL;
}


//-------------------------------------------------------------------
// GetInputStreamInfo
// Returns information about an input stream.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetInputStreamInfo(
    DWORD                     dwInputStreamID,
    MFT_INPUT_STREAM_INFO *   pStreamInfo
)
{
    if (pStreamInfo == NULL)
    {
        return E_POINTER;
    }

    EnterCriticalSection(&m_critSec);

    if (!IsValidInputStream(dwInputStreamID))
    {
        LeaveCriticalSection(&m_critSec);
        return MF_E_INVALIDSTREAMNUMBER;
    }

    // NOTE: This method should succeed even when there is no media type on the
    //       stream. If there is no media type, we only need to fill in the dwFlags
    //       member of MFT_INPUT_STREAM_INFO. The other members depend on having a
    //       a valid media type.

    pStreamInfo->hnsMaxLatency = 0;
    pStreamInfo->dwFlags = MFT_INPUT_STREAM_WHOLE_SAMPLES | MFT_INPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER;

    if (m_pInputType == NULL)
    {
        pStreamInfo->cbSize = 0;
    }
    else
    {
        pStreamInfo->cbSize = m_cbImageSize;
    }

    pStreamInfo->cbMaxLookahead = 0;
    pStreamInfo->cbAlignment = 0;

    LeaveCriticalSection(&m_critSec);
    return S_OK;
}

//-------------------------------------------------------------------
// GetOutputStreamInfo
// Returns information about an output stream.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetOutputStreamInfo(
    DWORD                     dwOutputStreamID,
    MFT_OUTPUT_STREAM_INFO *  pStreamInfo
)
{
    if (pStreamInfo == NULL)
    {
        return E_POINTER;
    }

    EnterCriticalSection(&m_critSec);

    if (!IsValidOutputStream(dwOutputStreamID))
    {
        LeaveCriticalSection(&m_critSec);
        return MF_E_INVALIDSTREAMNUMBER;
    }

    // NOTE: This method should succeed even when there is no media type on the
    //       stream. If there is no media type, we only need to fill in the dwFlags
    //       member of MFT_OUTPUT_STREAM_INFO. The other members depend on having a
    //       a valid media type.

    pStreamInfo->dwFlags =
        MFT_OUTPUT_STREAM_WHOLE_SAMPLES |
        MFT_OUTPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER |
        MFT_OUTPUT_STREAM_FIXED_SAMPLE_SIZE ;

    if (m_pOutputType == NULL)
    {
        pStreamInfo->cbSize = 0;
    }
    else
    {
        pStreamInfo->cbSize = m_cbImageSize;
    }

    pStreamInfo->cbAlignment = 0;

    LeaveCriticalSection(&m_critSec);
    return S_OK;
}


//-------------------------------------------------------------------
// GetAttributes
// Returns the attributes for the MFT.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetAttributes(IMFAttributes** ppAttributes)
{
    if (ppAttributes == NULL)
    {
        return E_POINTER;
    }

    EnterCriticalSection(&m_critSec);

    *ppAttributes = m_pAttributes;
    (*ppAttributes)->AddRef();

    LeaveCriticalSection(&m_critSec);
    return S_OK;
}


//-------------------------------------------------------------------
// GetInputStreamAttributes
// Returns stream-level attributes for an input stream.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetInputStreamAttributes(
    DWORD           dwInputStreamID,
    IMFAttributes   **ppAttributes
)
{
    // This MFT does not support any stream-level attributes, so the method is not implemented.
    return E_NOTIMPL;
}


//-------------------------------------------------------------------
// GetOutputStreamAttributes
// Returns stream-level attributes for an output stream.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetOutputStreamAttributes(
    DWORD           dwOutputStreamID,
    IMFAttributes   **ppAttributes
)
{
    // This MFT does not support any stream-level attributes, so the method is not implemented.
    return E_NOTIMPL;
}


//-------------------------------------------------------------------
// DeleteInputStream
//-------------------------------------------------------------------

HRESULT CGrayscale::DeleteInputStream(DWORD dwStreamID)
{
    // This MFT has a fixed number of input streams, so the method is not supported.
    return E_NOTIMPL;
}


//-------------------------------------------------------------------
// AddInputStreams
//-------------------------------------------------------------------

HRESULT CGrayscale::AddInputStreams(
    DWORD   cStreams,
    DWORD   *adwStreamIDs
)
{
    // This MFT has a fixed number of output streams, so the method is not supported.
    return E_NOTIMPL;
}


//-------------------------------------------------------------------
// GetInputAvailableType
// Returns a preferred input type.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetInputAvailableType(
    DWORD           dwInputStreamID,
    DWORD           dwTypeIndex, // 0-based
    IMFMediaType    **ppType
)
{
    if (ppType == NULL)
    {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&m_critSec);

    if (!IsValidInputStream(dwInputStreamID))
    {
        LeaveCriticalSection(&m_critSec);
        return MF_E_INVALIDSTREAMNUMBER;
    }

    HRESULT hr = S_OK;

    // If the output type is set, return that type as our preferred input type.
    if (m_pOutputType == NULL)
    {
        // The output type is not set. Create a partial media type.
        hr = OnGetPartialType(dwTypeIndex, ppType);
    }
    else if (dwTypeIndex > 0)
    {
        hr = MF_E_NO_MORE_TYPES;
    }
    else
    {
        *ppType = m_pOutputType;
        (*ppType)->AddRef();
    }

    LeaveCriticalSection(&m_critSec);
    return hr;
}



//-------------------------------------------------------------------
// GetOutputAvailableType
// Returns a preferred output type.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetOutputAvailableType(
    DWORD           dwOutputStreamID,
    DWORD           dwTypeIndex, // 0-based
    IMFMediaType    **ppType
)
{
    if (ppType == NULL)
    {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&m_critSec);

    if (!IsValidOutputStream(dwOutputStreamID))
    {
        LeaveCriticalSection(&m_critSec);
        return MF_E_INVALIDSTREAMNUMBER;
    }

    HRESULT hr = S_OK;

    if (m_pInputType == NULL)
    {
        // The input type is not set. Create a partial media type.
        hr = OnGetPartialType(dwTypeIndex, ppType);
    }
    else if (dwTypeIndex > 0)
    {
        hr = MF_E_NO_MORE_TYPES;
    }
    else
    {
        *ppType = m_pInputType;
        (*ppType)->AddRef();
    }

    LeaveCriticalSection(&m_critSec);
    return hr;
}


//-------------------------------------------------------------------
// SetInputType
//-------------------------------------------------------------------

HRESULT CGrayscale::SetInputType(
    DWORD           dwInputStreamID,
    IMFMediaType    *pType, // Can be NULL to clear the input type.
    DWORD           dwFlags
)
{
    // Validate flags.
    if (dwFlags & ~MFT_SET_TYPE_TEST_ONLY)
    {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&m_critSec);

    if (!IsValidInputStream(dwInputStreamID))
    {
        LeaveCriticalSection(&m_critSec);
        return MF_E_INVALIDSTREAMNUMBER;
    }

    HRESULT hr = S_OK;

    // Does the caller want us to set the type, or just test it?
    BOOL bReallySet = ((dwFlags & MFT_SET_TYPE_TEST_ONLY) == 0);

    // If we have an input sample, the client cannot change the type now.
    if (HasPendingOutput())
    {
        hr = MF_E_TRANSFORM_CANNOT_CHANGE_MEDIATYPE_WHILE_PROCESSING;
        goto done;
    }

    // Validate the type, if non-NULL.
    if (pType)
    {
        hr = OnCheckInputType(pType);
        if (FAILED(hr))
        {
            goto done;
        }
    }

    // The type is OK. Set the type, unless the caller was just testing.
    if (bReallySet)
    {
        OnSetInputType(pType);

        // When the type changes, end streaming.
        hr = EndStreaming();
    }

done:
    LeaveCriticalSection(&m_critSec);
    return hr;
}



//-------------------------------------------------------------------
// SetOutputType
//-------------------------------------------------------------------

HRESULT CGrayscale::SetOutputType(
    DWORD           dwOutputStreamID,
    IMFMediaType    *pType, // Can be NULL to clear the output type.
    DWORD           dwFlags
)
{
    // Validate flags.
    if (dwFlags & ~MFT_SET_TYPE_TEST_ONLY)
    {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&m_critSec);

    if (!IsValidOutputStream(dwOutputStreamID))
    {
        LeaveCriticalSection(&m_critSec);
        return MF_E_INVALIDSTREAMNUMBER;
    }

    HRESULT hr = S_OK;

    // Does the caller want us to set the type, or just test it?
    BOOL bReallySet = ((dwFlags & MFT_SET_TYPE_TEST_ONLY) == 0);

    // If we have an input sample, the client cannot change the type now.
    if (HasPendingOutput())
    {
        hr = MF_E_TRANSFORM_CANNOT_CHANGE_MEDIATYPE_WHILE_PROCESSING;
        goto done;
    }

    // Validate the type, if non-NULL.
    if (pType)
    {
        hr = OnCheckOutputType(pType);
        if (FAILED(hr))
        {
            goto done;
        }
    }

    // The type is OK. Set the type, unless the caller was just testing.
    if (bReallySet)
    {
        OnSetOutputType(pType);

        // When the type changes, end streaming.
        hr = EndStreaming();
    }

done:
    LeaveCriticalSection(&m_critSec);
    return hr;
}


//-------------------------------------------------------------------
// GetInputCurrentType
// Returns the current input type.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetInputCurrentType(
    DWORD           dwInputStreamID,
    IMFMediaType    **ppType
)
{
    if (ppType == NULL)
    {
        return E_POINTER;
    }

    HRESULT hr = S_OK;

    EnterCriticalSection(&m_critSec);

    if (!IsValidInputStream(dwInputStreamID))
    {
        hr = MF_E_INVALIDSTREAMNUMBER;
    }
    else if (!m_pInputType)
    {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    else
    {
        *ppType = m_pInputType;
        (*ppType)->AddRef();
    }
    LeaveCriticalSection(&m_critSec);
    return hr;
}


//-------------------------------------------------------------------
// GetOutputCurrentType
// Returns the current output type.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetOutputCurrentType(
    DWORD           dwOutputStreamID,
    IMFMediaType    **ppType
)
{
    if (ppType == NULL)
    {
        return E_POINTER;
    }

    HRESULT hr = S_OK;

    EnterCriticalSection(&m_critSec);

    if (!IsValidOutputStream(dwOutputStreamID))
    {
        hr = MF_E_INVALIDSTREAMNUMBER;
    }
    else if (!m_pOutputType)
    {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    else
    {
        *ppType = m_pOutputType;
        (*ppType)->AddRef();
    }

    LeaveCriticalSection(&m_critSec);
    return hr;
}


//-------------------------------------------------------------------
// GetInputStatus
// Query if the MFT is accepting more input.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetInputStatus(
    DWORD           dwInputStreamID,
    DWORD           *pdwFlags
)
{
    if (pdwFlags == NULL)
    {
        return E_POINTER;
    }

    EnterCriticalSection(&m_critSec);

    if (!IsValidInputStream(dwInputStreamID))
    {
        LeaveCriticalSection(&m_critSec);
        return MF_E_INVALIDSTREAMNUMBER;
    }

    // If an input sample is already queued, do not accept another sample until the 
    // client calls ProcessOutput or Flush.

    // NOTE: It is possible for an MFT to accept more than one input sample. For 
    // example, this might be required in a video decoder if the frames do not 
    // arrive in temporal order. In the case, the decoder must hold a queue of 
    // samples. For the video effect, each sample is transformed independently, so
    // there is no reason to queue multiple input samples.

    if (m_pSample == NULL)
    {
        *pdwFlags = MFT_INPUT_STATUS_ACCEPT_DATA;
    }
    else
    {
        *pdwFlags = 0;
    }

    LeaveCriticalSection(&m_critSec);
    return S_OK;
}



//-------------------------------------------------------------------
// GetOutputStatus
// Query if the MFT can produce output.
//-------------------------------------------------------------------

HRESULT CGrayscale::GetOutputStatus(DWORD *pdwFlags)
{
    if (pdwFlags == NULL)
    {
        return E_POINTER;
    }

    EnterCriticalSection(&m_critSec);

    // The MFT can produce an output sample if (and only if) there an input sample.
    if (m_pSample != NULL)
    {
        *pdwFlags = MFT_OUTPUT_STATUS_SAMPLE_READY;
    }
    else
    {
        *pdwFlags = 0;
    }

    LeaveCriticalSection(&m_critSec);
    return S_OK;
}


//-------------------------------------------------------------------
// SetOutputBounds
// Sets the range of time stamps that the MFT will output.
//-------------------------------------------------------------------

HRESULT CGrayscale::SetOutputBounds(
    LONGLONG        hnsLowerBound,
    LONGLONG        hnsUpperBound
)
{
    // Implementation of this method is optional.
    return E_NOTIMPL;
}


//-------------------------------------------------------------------
// ProcessEvent
// Sends an event to an input stream.
//-------------------------------------------------------------------

HRESULT CGrayscale::ProcessEvent(
    DWORD              dwInputStreamID,
    IMFMediaEvent      *pEvent
)
{
    // This MFT does not handle any stream events, so the method can
    // return E_NOTIMPL. This tells the pipeline that it can stop
    // sending any more events to this MFT.
    return E_NOTIMPL;
}


//-------------------------------------------------------------------
// ProcessMessage
//-------------------------------------------------------------------

HRESULT CGrayscale::ProcessMessage(
    MFT_MESSAGE_TYPE    eMessage,
    ULONG_PTR           ulParam
)
{
    EnterCriticalSection(&m_critSec);

    HRESULT hr = S_OK;

    switch (eMessage)
    {
    case MFT_MESSAGE_COMMAND_FLUSH:
        // Flush the MFT.
        hr = OnFlush();
        break;

    case MFT_MESSAGE_COMMAND_DRAIN:
        // Drain: Tells the MFT to reject further input until all pending samples are
        // processed. That is our default behavior already, so there is nothing to do.
        //
        // For a decoder that accepts a queue of samples, the MFT might need to drain
        // the queue in response to this command.
    break;

    case MFT_MESSAGE_SET_D3D_MANAGER:
        // Sets a pointer to the IDirect3DDeviceManager9 interface.

        // The pipeline should never send this message unless the MFT sets the MF_SA_D3D_AWARE 
        // attribute set to TRUE. Because this MFT does not set MF_SA_D3D_AWARE, it is an error
        // to send the MFT_MESSAGE_SET_D3D_MANAGER message to the MFT. Return an error code in
        // this case.

        // NOTE: If this MFT were D3D-enabled, it would cache the IDirect3DDeviceManager9 
        // pointer for use during streaming.

        hr = E_NOTIMPL;
        break;

    case MFT_MESSAGE_NOTIFY_BEGIN_STREAMING:
        hr = BeginStreaming();
        break;

    case MFT_MESSAGE_NOTIFY_END_STREAMING:
        hr = EndStreaming();
        break;

    // The next two messages do not require any action from this MFT.

    case MFT_MESSAGE_NOTIFY_END_OF_STREAM:
        break;

    case MFT_MESSAGE_NOTIFY_START_OF_STREAM:
        break;
    }

    LeaveCriticalSection(&m_critSec);
    return hr;
}


//-------------------------------------------------------------------
// ProcessInput
// Process an input sample.
//-------------------------------------------------------------------

HRESULT CGrayscale::ProcessInput(
    DWORD               dwInputStreamID,
    IMFSample           *pSample,
    DWORD               dwFlags
)
{
    // Check input parameters.
    if (pSample == NULL)
    {
        return E_POINTER;
    }

    if (dwFlags != 0)
    {
        return E_INVALIDARG; // dwFlags is reserved and must be zero.
    }

    HRESULT hr = S_OK;

    EnterCriticalSection(&m_critSec);

    // Validate the input stream number.
    if (!IsValidInputStream(dwInputStreamID))
    {
        hr = MF_E_INVALIDSTREAMNUMBER;
        goto done;
    }

    // Check for valid media types.
    // The client must set input and output types before calling ProcessInput.
    if (!m_pInputType || !m_pOutputType)
    {
        hr = MF_E_NOTACCEPTING;   
        goto done;
    }

    // Check if an input sample is already queued.
    if (m_pSample != NULL)
    {
        hr = MF_E_NOTACCEPTING;   // We already have an input sample.
        goto done;
    }

    // Initialize streaming.
    hr = BeginStreaming();
    if (FAILED(hr))
    {
        goto done;
    }

    // Cache the sample. We do the actual work in ProcessOutput.
    m_pSample = pSample;
    pSample->AddRef();  // Hold a reference count on the sample.

done:
    LeaveCriticalSection(&m_critSec);
    return hr;
}


//-------------------------------------------------------------------
// ProcessOutput
// Process an output sample.
//-------------------------------------------------------------------

HRESULT CGrayscale::ProcessOutput(
    DWORD                   dwFlags,
    DWORD                   cOutputBufferCount,
    MFT_OUTPUT_DATA_BUFFER  *pOutputSamples, // one per stream
    DWORD                   *pdwStatus
)
{
    // Check input parameters...

    // This MFT does not accept any flags for the dwFlags parameter.

    // The only defined flag is MFT_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER. This flag 
    // applies only when the MFT marks an output stream as lazy or optional. But this
    // MFT has no lazy or optional streams, so the flag is not valid.

    if (dwFlags != 0)
    {
        return E_INVALIDARG;
    }

    if (pOutputSamples == NULL || pdwStatus == NULL)
    {
        return E_POINTER;
    }

    // There must be exactly one output buffer.
    if (cOutputBufferCount != 1)
    {
        return E_INVALIDARG;
    }

    // It must contain a sample.
    if (pOutputSamples[0].pSample == NULL)
    {
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;

    IMFMediaBuffer *pInput = NULL;
    IMFMediaBuffer *pOutput = NULL;

    EnterCriticalSection(&m_critSec);

    // There must be an input sample available for processing.
    if (m_pSample == NULL)
    {
        hr = MF_E_TRANSFORM_NEED_MORE_INPUT;
        goto done;
    }

    // Initialize streaming.

    hr = BeginStreaming();
    if (FAILED(hr))
    {
        goto done;
    }

    // Get the input buffer.
    hr = m_pSample->ConvertToContiguousBuffer(&pInput);
    if (FAILED(hr))
    {
        goto done;
    }

    // Get the output buffer.
    hr = pOutputSamples[0].pSample->ConvertToContiguousBuffer(&pOutput);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = OnProcessOutput(pInput, pOutput);
    if (FAILED(hr))
    {
        goto done;
    }

    // Set status flags.
    pOutputSamples[0].dwStatus = 0;
    *pdwStatus = 0;


    // Copy the duration and time stamp from the input sample, if present.

    LONGLONG hnsDuration = 0;
    LONGLONG hnsTime = 0;

    if (SUCCEEDED(m_pSample->GetSampleDuration(&hnsDuration)))
    {
        hr = pOutputSamples[0].pSample->SetSampleDuration(hnsDuration);
        if (FAILED(hr))
        {
            goto done;
        }
    }

    if (SUCCEEDED(m_pSample->GetSampleTime(&hnsTime)))
    {
        hr = pOutputSamples[0].pSample->SetSampleTime(hnsTime);
    }

done:
    SafeRelease(&m_pSample);   // Release our input sample.
    SafeRelease(&pInput);
    SafeRelease(&pOutput);
    LeaveCriticalSection(&m_critSec);
    return hr;
}

// PRIVATE METHODS

// All methods that follow are private to this MFT and are not part of the IMFTransform interface.

// Create a partial media type from our list.
//
// dwTypeIndex: Index into the list of peferred media types.
// ppmt:        Receives a pointer to the media type.

HRESULT CGrayscale::OnGetPartialType(DWORD dwTypeIndex, IMFMediaType **ppmt)
{
    if (dwTypeIndex >= ARRAYSIZE(g_MediaSubtypes))
    {
        return MF_E_NO_MORE_TYPES;
    }

    IMFMediaType *pmt = NULL;

    HRESULT hr = MFCreateMediaType(&pmt);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pmt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr))
    {
        goto done;
    }

    hr = pmt->SetGUID(MF_MT_SUBTYPE, g_MediaSubtypes[dwTypeIndex]);
    if (FAILED(hr))
    {
        goto done;
    }

    *ppmt = pmt;
    (*ppmt)->AddRef();

done:
    SafeRelease(&pmt);
    return hr;
}


// Validate an input media type.

HRESULT CGrayscale::OnCheckInputType(IMFMediaType *pmt)
{
    assert(pmt != NULL);

    HRESULT hr = S_OK;

    // If the output type is set, see if they match.
    if (m_pOutputType != NULL)
    {
        DWORD flags = 0;
        hr = pmt->IsEqual(m_pOutputType, &flags);

        // IsEqual can return S_FALSE. Treat this as failure.
        if (hr != S_OK)
        {
            hr = MF_E_INVALIDMEDIATYPE;
        }
    }
    else
    {
        // Output type is not set. Just check this type.
        hr = OnCheckMediaType(pmt);
    }
    return hr;
}


// Validate an output media type.

HRESULT CGrayscale::OnCheckOutputType(IMFMediaType *pmt)
{
    assert(pmt != NULL);

    HRESULT hr = S_OK;

    // If the input type is set, see if they match.
    if (m_pInputType != NULL)
    {
        DWORD flags = 0;
        hr = pmt->IsEqual(m_pInputType, &flags);

        // IsEqual can return S_FALSE. Treat this as failure.
        if (hr != S_OK)
        {
            hr = MF_E_INVALIDMEDIATYPE;
        }

    }
    else
    {
        // Input type is not set. Just check this type.
        hr = OnCheckMediaType(pmt);
    }
    return hr;
}


// Validate a media type (input or output)

HRESULT CGrayscale::OnCheckMediaType(IMFMediaType *pmt)
{
    BOOL bFoundMatchingSubtype = FALSE;

    // Major type must be video.
    GUID major_type;
    HRESULT hr = pmt->GetGUID(MF_MT_MAJOR_TYPE, &major_type);
    if (FAILED(hr))
    {
        goto done;
    }

    if (major_type != MFMediaType_Video)
    {
        hr = MF_E_INVALIDMEDIATYPE;
        goto done;
    }

    // Subtype must be one of the subtypes in our global list.

    // Get the subtype GUID.
    GUID subtype;
    hr = pmt->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr))
    {
        goto done;
    }

    // Look for the subtype in our list of accepted types.
    for (DWORD i = 0; i < ARRAYSIZE(g_MediaSubtypes); i++)
    {
        if (subtype == g_MediaSubtypes[i])
        {
            bFoundMatchingSubtype = TRUE;
            break;
        }
    }

    if (!bFoundMatchingSubtype)
    {
        hr = MF_E_INVALIDMEDIATYPE; // The MFT does not support this subtype.
        goto done;
    }

    // Reject single-field media types. 
    UINT32 interlace = MFGetAttributeUINT32(pmt, MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (interlace == MFVideoInterlace_FieldSingleUpper  || interlace == MFVideoInterlace_FieldSingleLower)
    {
        hr = MF_E_INVALIDMEDIATYPE;
    }

done:
    return hr;
}


// Set or clear the input media type.
//
// Prerequisite: The input type was already validated.

void CGrayscale::OnSetInputType(IMFMediaType *pmt)
{
    // if pmt is NULL, clear the type.
    // if pmt is non-NULL, set the type.

    SafeRelease(&m_pInputType);
    m_pInputType = pmt;
    if (m_pInputType)
    {
        m_pInputType->AddRef();
    }

    // Update the format information.
    UpdateFormatInfo();
}


// Set or clears the output media type.
//
// Prerequisite: The output type was already validated.

void CGrayscale::OnSetOutputType(IMFMediaType *pmt)
{
    // If pmt is NULL, clear the type. Otherwise, set the type.

    SafeRelease(&m_pOutputType);
    m_pOutputType = pmt;
    if (m_pOutputType)
    {
        m_pOutputType->AddRef();
    }
}


// Initialize streaming parameters.
//
// This method is called if the client sends the MFT_MESSAGE_NOTIFY_BEGIN_STREAMING
// message, or when the client processes a sample, whichever happens first.

HRESULT CGrayscale::BeginStreaming()
{
    HRESULT hr = S_OK;

    if (!m_bStreamingInitialized)
    {
        // Get the configuration attributes.

        // Get the destination rectangle.

        RECT rcDest;
        hr = m_pAttributes->GetBlob(MFT_GRAYSCALE_DESTINATION_RECT, (UINT8*)&rcDest, sizeof(rcDest), NULL);
        if (hr == MF_E_ATTRIBUTENOTFOUND || !ValidateRect(rcDest))
        {
            // The client did not set this attribute, or the client provided an invalid rectangle.
            // Default to the entire image.

            m_rcDest = D2D1::RectU(0, 0, m_imageWidthInPixels, m_imageHeightInPixels);
            hr = S_OK;
        }
        else if (SUCCEEDED(hr))
        {
            m_rcDest = D2D1::RectU(rcDest.left, rcDest.top, rcDest.right, rcDest.bottom);
        }
        else
        {
            goto done;
        }

        // Get the chroma transformations.

        float scale = (float)MFGetAttributeDouble(m_pAttributes, MFT_GRAYSCALE_SATURATION, 0.0f);
        float angle = (float)MFGetAttributeDouble(m_pAttributes, MFT_GRAYSCALE_CHROMA_ROTATION, 0.0f);

        m_transform = D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Rotation(angle);

        m_bStreamingInitialized = true;
    }

done:
    return hr;
}


// End streaming. 

// This method is called if the client sends an MFT_MESSAGE_NOTIFY_END_STREAMING
// message, or when the media type changes. In general, it should be called whenever
// the streaming parameters need to be reset.

HRESULT CGrayscale::EndStreaming()
{
    m_bStreamingInitialized = false;
    return S_OK;
}



// Generate output data.

HRESULT CGrayscale::OnProcessOutput(IMFMediaBuffer *pIn, IMFMediaBuffer *pOut)
{
    BYTE *pDest = NULL;         // Destination buffer.
    LONG lDestStride = 0;       // Destination stride.

    BYTE *pSrc = NULL;          // Source buffer.
    LONG lSrcStride = 0;        // Source stride.

    // Helper objects to lock the buffers.
    VideoBufferLock inputLock(pIn);
    VideoBufferLock outputLock(pOut);

    // Stride if the buffer does not support IMF2DBuffer
    LONG lDefaultStride = 0;

    HRESULT hr = GetDefaultStride(m_pInputType, &lDefaultStride);
    if (FAILED(hr))
    {
        goto done;
    }

    // Lock the input buffer.
    hr = inputLock.LockBuffer(lDefaultStride, m_imageHeightInPixels, &pSrc, &lSrcStride);
    if (FAILED(hr))
    {
        goto done;
    }

    // Lock the output buffer.
    hr = outputLock.LockBuffer(lDefaultStride, m_imageHeightInPixels, &pDest, &lDestStride);
    if (FAILED(hr))
    {
        goto done;
    }

    // Invoke the image transform function.
    assert (m_pTransformFn != NULL);
    if (m_pTransformFn)
    {
        (*m_pTransformFn)(m_transform, m_rcDest, pDest, lDestStride, pSrc, lSrcStride,
            m_imageWidthInPixels, m_imageHeightInPixels, m_pFilteredYSrc);
    }
    else
    {
        hr = E_UNEXPECTED;
        goto done;
    }


    // Set the data size on the output buffer.
    hr = pOut->SetCurrentLength(m_cbImageSize);

    // The VideoBufferLock class automatically unlocks the buffers.
done:
    return hr;
}


// Flush the MFT.

HRESULT CGrayscale::OnFlush()
{
    // For this MFT, flushing just means releasing the input sample.
    SafeRelease(&m_pSample);
    return S_OK;
}


// Update the format information. This method is called whenever the
// input type is set.

HRESULT CGrayscale::UpdateFormatInfo()
{
    HRESULT hr = S_OK;

    GUID subtype = GUID_NULL;

    m_imageWidthInPixels = 0;
    m_imageHeightInPixels = 0;
    m_cbImageSize = 0;

    m_pTransformFn = NULL;
	m_pFilteredYSrc= NULL;

    if (m_pInputType != NULL)
    {
        hr = m_pInputType->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (FAILED(hr))
        {
            goto done;
        }

		hr = MFGetAttributeSize(m_pInputType, MF_MT_FRAME_SIZE, &m_imageWidthInPixels, &m_imageHeightInPixels);
        if (FAILED(hr))
        {
            goto done;
        }

		m_pFilteredYSrc = (BYTE *)calloc(m_imageHeightInPixels*m_imageWidthInPixels, sizeof(BYTE));
        if (subtype == MFVideoFormat_YUY2)
        {
            //m_pTransformFn = TransformImage_YUY2;
			m_pTransformFn = (NULL== m_pFilteredYSrc) ? EdgeDectection_YUY2 : EdgeDectectionF_YUY2;
        }
        else if (subtype == MFVideoFormat_UYVY)
        {
            //m_pTransformFn = TransformImage_UYVY;
			m_pTransformFn = (NULL== m_pFilteredYSrc) ? EdgeDectection_UYVY : EdgeDectectionF_UYVY;
        }
        else if (subtype == MFVideoFormat_NV12)
        {
            m_pTransformFn = (NULL== m_pFilteredYSrc) ? EdgeDectection_NV12 : EdgeDectectionF_NV12;
        }
        else
        {
            hr = E_UNEXPECTED;
            goto done;
        }

        // Calculate the image size (not including padding)
        hr = GetImageSize(subtype.Data1, m_imageWidthInPixels, m_imageHeightInPixels, &m_cbImageSize);
    }

done:
    return hr;
}


// Calculate the size of the buffer needed to store the image.

// fcc: The FOURCC code of the video format.

HRESULT GetImageSize(DWORD fcc, UINT32 width, UINT32 height, DWORD* pcbImage)
{
    HRESULT hr = S_OK;

    switch (fcc)
    {
    case FOURCC_YUY2:
    case FOURCC_UYVY:
        // check overflow
        if ((width > MAXDWORD / 2) || (width * 2 > MAXDWORD / height))
        {
            hr = E_INVALIDARG;
        }
        else
        {   
            // 16 bpp
            *pcbImage = width * height * 2;
        }
        break;

    case FOURCC_NV12:
        // check overflow
        if ((height/2 > MAXDWORD - height) || ((height + height/2) > MAXDWORD / width))
        {
            hr = E_INVALIDARG;
        }
        else
        {
            // 12 bpp
            *pcbImage = width * (height + (height/2));
        }
        break;

    default:
        hr = E_FAIL;    // Unsupported type.
    }
    return hr;
}

// Get the default stride for a video format. 
HRESULT GetDefaultStride(IMFMediaType *pType, LONG *plStride)
{
    LONG lStride = 0;

    // Try to get the default stride from the media type.
    HRESULT hr = pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&lStride);
    if (FAILED(hr))
    {
        // Attribute not set. Try to calculate the default stride.
        GUID subtype = GUID_NULL;

        UINT32 width = 0;
        UINT32 height = 0;

        // Get the subtype and the image size.
        hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (SUCCEEDED(hr))
        {
            hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
        }
        if (SUCCEEDED(hr))
        {
            if (subtype == MFVideoFormat_NV12)
            {
                lStride = width;
            }
            else if (subtype == MFVideoFormat_YUY2 || subtype == MFVideoFormat_UYVY)
            {
                lStride = ((width * 2) + 3) & ~3;
            }
            else
            {
                hr = E_INVALIDARG;
            }
        }

        // Set the attribute for later reference.
        if (SUCCEEDED(hr))
        {
            (void)pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(lStride));
        }
    }
    if (SUCCEEDED(hr))
    {
        *plStride = lStride;
    }
    return hr;
}


// Validate that a rectangle meets the following criteria:
//
//  - All coordinates are non-negative.
//  - The rectangle is not flipped (top > bottom, left > right)
//
// These are the requirements for the destination rectangle.

bool ValidateRect(const RECT& rc)
{
    if (rc.left < 0 || rc.top < 0)
    {
        return false;
    }
    if (rc.left > rc.right || rc.top > rc.bottom)
    {
        return false;
    }
    return true;
}
