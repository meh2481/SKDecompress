//Created by Mark H on 6/1/15
//Do whatever you want with this code
#include "wfLZ.h"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <stdlib.h>
#include <cstdio>
#include <squish.h>
#ifdef _WIN32
	#include <windows.h>
#endif
#include "FreeImage.h"
#include <list>
#include <cmath>
#include <cstring>
#include <iomanip>
using namespace std;

typedef struct
{
	uint32_t type;
	uint32_t numChildren;
	uint64_t childPtr;	//Points to a list of numChildren childNodePtrs
	//Followed by additional info depending on type
}dataVert;

typedef struct
{
	uint64_t offset;	//Points to next child dataVert
}childNodePtr;

//Different values for dataVert.type
#define VERT_TYPE_NONE			0
#define VERT_TYPE_TEX			1
#define VERT_TYPE_VBO			2
#define VERT_TYPE_PROP			3
#define VERT_TYPE_PROP_SCALAR	4
#define VERT_TYPE_PROP_PT		5
#define VERT_TYPE_PROP_ANCHOR	6
#define VERT_TYPE_PROP_RECT		7
#define VERT_TYPE_PROP_STR		8
#define VERT_TYPE_PROP_TAB		9
#define VERT_TYPE_FRAME			10
#define VERT_TYPE_SEQ_FRAME		11
#define VERT_TYPE_SEQ			12
#define VERT_TYPE_ANIM			13

//Top of ANB file
typedef struct
{
	uint8_t sig[4];			//Should be "YCSN"
	uint32_t unknown1;		//Always 0
	uint32_t unknown2;		//Always 1
	uint32_t unknown3[3];	//All 0
	dataVert head;			//Root of our data tree
}anbHeader;

//Additional info following dataVert if dataVert.type == VERT_TYPE_TEX
typedef struct
{
	uint32_t width;
	uint32_t height;
	uint32_t flags;
	uint32_t unk;
	uint64_t dataPtr;	//Point to texHeader
}texVert;

//Additional info following dataVert if dataVert.type == VERT_TYPE_VBO
typedef struct
{
	uint32_t num;
	uint32_t flags;
	uint64_t dataPtr;	//Point to pieceHeader
}vboVert;

//Additional info following dataVert if dataVert.type == VERT_TYPE_SEQ
typedef struct
{
	uint32_t id;
	uint32_t numFrames;
}frameSeq;

//Additional info following dataVert if dataVert.type == VERT_TYPE_SEQ_FRAME
typedef struct
{
	uint32_t frameNo;
	float delay;
}frame;

//Additional info following dataVert if dataVert.type == VERT_TYPE_FRAME
typedef struct
{
	float minx;
	float maxx;
	float miny;
	float maxy;
}frameVert;

typedef struct
{
	uint32_t unk;				//0xFFFFFF, so maybe RGB mask?
	uint32_t compressedSize;	//Maybe? We don't care, anyway
	//uint8_t data[]			//Followed by WFLZ-compressed image data
} texHeader;

typedef struct
{
	uint32_t unk; 	//Always 0xFFFFFF
	uint32_t size;	//32 if num pieces = 1, 64 if num pieces = 2, etc
	//piece[]		//followed by vboVert.num pieces
}pieceHeader;

typedef struct
{
	float x;
	float y;
} Vec2;

typedef struct
{
	uint16_t x;
	uint16_t y;
} VecInt;
/*typedef struct 
{
	Vec2 topRight;
	Vec2 bottomLeft;
	Vec2 topRightUV;
	Vec2 bottomLeftUV;
} piece;*/
typedef struct
{
	Vec2 topLeft;
	VecInt topLeftUV;
	uint16_t width, height;
} piece;


vector<frameVert> animSizes;	//How large the largest piece in every anim is
vector<list<piece> > imgPieces;
vector<texVert> imgHeader;
int iNumFiles;
string sCurFileName;
int iNumFrameSequences;
vector<list<frame> > frameSequences;

bool g_bGreenBg;
bool g_bCreateIcon;
int offsetX = 2;
int offsetY = 2;
int iconW = 148;
int iconH = 125;

//-------------------------------------------------------------------------------------------------------
// Functions
//-------------------------------------------------------------------------------------------------------

int powerof2(int orig)
{
	int result = 1;
	while(result < orig)
		result <<= 1;
	return result;
}

/*FIBITMAP* imageFromPixels(uint8_t* imgData, uint32_t width, uint32_t height)
{
	//FreeImage is broken here and you can't swap R/G/B channels upon creation. Do that manually
	FIBITMAP* result = FreeImage_ConvertFromRawBits(imgData, width, height, ((((32 * width) + 31) / 32) * 4), 32, FI_RGBA_RED, FI_RGBA_GREEN, FI_RGBA_BLUE, true);
	FIBITMAP* r = FreeImage_GetChannel(result, FICC_RED);
	FIBITMAP* b = FreeImage_GetChannel(result, FICC_BLUE);
	FreeImage_SetChannel(result, b, FICC_RED);
	FreeImage_SetChannel(result, r, FICC_BLUE);
	FreeImage_Unload(r);
	FreeImage_Unload(b);
	return result;
}*/
FIBITMAP* imageFromPixels(uint8_t* imgData, uint32_t width, uint32_t height)
{
	//return FreeImage_ConvertFromRawBits(imgData, width, height, width*4, 32, 0xFF0000, 0x00FF00, 0x0000FF, true);	//Doesn't seem to work
	FIBITMAP* curImg = FreeImage_Allocate(width, height, 32);
	FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(curImg);
	if(image_type == FIT_BITMAP)
	{
		int curPos = 0;
		unsigned pitch = FreeImage_GetPitch(curImg);
		BYTE* bits = (BYTE*)FreeImage_GetBits(curImg);
		bits += pitch * height - pitch;
		for(int y = height-1; y >= 0; y--)
		{
			BYTE* pixel = (BYTE*)bits;
			for(int x = 0; x < width; x++)
			{
				if(g_bGreenBg && imgData[curPos+3] == 0)
				{
					pixel[FI_RGBA_RED] = 0;
					pixel[FI_RGBA_GREEN] = 255;
					pixel[FI_RGBA_BLUE] = 0;
					pixel[FI_RGBA_ALPHA] = 255;
					curPos+=4;
				}
				else
				{
					pixel[FI_RGBA_RED] = imgData[curPos++];
					pixel[FI_RGBA_GREEN] = imgData[curPos++];
					pixel[FI_RGBA_BLUE] = imgData[curPos++];
					pixel[FI_RGBA_ALPHA] = imgData[curPos++];
				}
				pixel += 4;
			}
			bits -= pitch;
		}
	}
	return curImg;
}

FIBITMAP* PieceImage(uint8_t* imgData, list<piece> pieces, Vec2 maxul, Vec2 maxbr, texVert th)
{
	Vec2 OutputSize;
	Vec2 CenterPos;
	OutputSize.x = -maxul.x + maxbr.x;
	OutputSize.y = -maxul.y + maxbr.y;
	CenterPos.x = -maxul.x + 50;
	CenterPos.y = -maxul.y + 50;
	//OutputSize.x = uint32_t(OutputSize.x);
	//OutputSize.y = uint32_t(OutputSize.y);

	//My math seems off, so rather than solving the problem, create larger than needed, then crop. Hooray!
	FIBITMAP* result = FreeImage_Allocate(OutputSize.x+100, OutputSize.y+100, 32);
	
	if(g_bGreenBg)
	{
		RGBQUAD q = {0,255,0,255};
		FreeImage_FillBackground(result, (const void *)&q);
	}

	//Create image from this set of pixels
	FIBITMAP* curImg = imageFromPixels(imgData, th.width, th.height);

	//Patch image together from pieces
	for(list<piece>::iterator lpi = pieces.begin(); lpi != pieces.end(); lpi++)
	{
		//cout << "Top left: " << lpi->topLeft.x << "," << lpi->topLeft.y << endl;
		//cout << "Top left UV: " << lpi->topLeftUV.x << "," << lpi->topLeftUV.y << endl;
		//cout << "w/h: " << lpi->width << "," << lpi->height << endl;
		//cout << "center: " << CenterPos.x << "," << CenterPos.y << endl;
		//cout << "maxul/br: " << maxul.x << "," << maxul.y << " " << maxbr.x << "," << maxbr.y << endl;
		
		FIBITMAP* imgPiece = FreeImage_Copy(curImg, 
											lpi->topLeftUV.x, lpi->topLeftUV.y, 
											lpi->topLeftUV.x + lpi->width, lpi->topLeftUV.y + lpi->height);
						
		FreeImage_Paste(result, imgPiece, CenterPos.x + lpi->topLeft.x, CenterPos.y + lpi->topLeft.y, 255);
		
		//return imgPiece;
		
		/*/Since pasting doesn't allow you to post an image onto a particular position of another, do that by hand
		//TODO: That's a false statement. Let FreeImage handle image pasting
		int curPos = 0;
		int srcW = FreeImage_GetWidth(imgPiece);
		int srcH = FreeImage_GetHeight(imgPiece);
		unsigned pitch = FreeImage_GetPitch(imgPiece);
		unsigned destpitch = FreeImage_GetPitch(result);
		BYTE* bits = (BYTE*)FreeImage_GetBits(imgPiece);
		BYTE* destBits = (BYTE*)FreeImage_GetBits(result);
		Vec2 DestPos = CenterPos;
		DestPos.x += lpi->topLeft.x;
		DestPos.y = OutputSize.y - srcH;
		DestPos.y -= CenterPos.y;
		DestPos.y += lpi->topLeft.y;
		DestPos.x = (unsigned int)(DestPos.x);
		DestPos.y = ceil(DestPos.y);
		for(int y = 0; y < srcH; y++)
		{
			BYTE* pixel = bits;
			BYTE* destpixel = destBits;
			destpixel += (unsigned)((DestPos.y + y + 3)) * destpitch;
			destpixel += (unsigned)((DestPos.x + 3) * 4);
			for(int x = 0; x < srcW; x++)
			{
				destpixel[FI_RGBA_RED] = pixel[FI_RGBA_RED];
				destpixel[FI_RGBA_GREEN] = pixel[FI_RGBA_GREEN];
				destpixel[FI_RGBA_BLUE] = pixel[FI_RGBA_BLUE];
				destpixel[FI_RGBA_ALPHA] = pixel[FI_RGBA_ALPHA];
				pixel += 4;
				destpixel += 4;
			}
			bits += pitch;
		}*/
		
		FreeImage_Unload(imgPiece);
	}
	FreeImage_Unload(curImg);
	
	//Crop edges from final image
	FIBITMAP* cropped = FreeImage_Copy(result, 50, 50, FreeImage_GetWidth(result)-50, FreeImage_GetHeight(result)-50);
	FreeImage_Unload(result);
	
	return cropped;
	//return result;
}

FIBITMAP* last;
uint8_t* dst = NULL;
bool bFileHit = false;
bool bRedo = false;
FIBITMAP* decompressFrame(uint8_t* data, int iFile)
{
	frameVert fv = animSizes[iFile];
	Vec2 maxUL;
	Vec2 maxBR;
	maxUL.x = fv.minx;
	maxUL.y = fv.miny;
	maxBR.x = fv.maxx;
	maxBR.y = fv.maxy;

	if(imgPieces.size() <= iFile || imgHeader.size() <= iFile) return NULL;	//Skip if we don't have enough headers...
	if(imgHeader[iFile].width <= 0 || imgHeader[iFile].height <= 0 || imgHeader[iFile].dataPtr <= 0) //If it's not a legit header, means all image data was in first one
	{
		if(!dst)
		{
			bRedo = true;
			return NULL;
		}
		FIBITMAP* result = PieceImage(dst, imgPieces[iFile], maxUL, maxBR, imgHeader[0]);
		if(!bFileHit)
		{
			bFileHit = true;
			//cout << "FILE HIT" << endl;
		}
		return result;
	}

	//Decompress WFLZ data - one pass now, no chunks
	const uint32_t decompressedSize = wfLZ_GetDecompressedSize(&(data[imgHeader[iFile].dataPtr+sizeof(texHeader)]));
	if(dst)
		free(dst);
	dst = (uint8_t*)malloc(decompressedSize);
	wfLZ_Decompress(&(data[imgHeader[iFile].dataPtr+sizeof(texHeader)]), dst);
	
	//Piece together	
	FIBITMAP* result = PieceImage(dst, imgPieces[iFile], maxUL, maxBR, imgHeader[iFile]);
	
	last = result;
	return result;
}

void checkVert(dataVert v, uint8_t* data, uint32_t offset)
{
	uint32_t nodeExtOffset = offset + sizeof(dataVert);
	switch(v.type)
	{
		case VERT_TYPE_NONE:
			//cout << endl;
			break;
			
		case VERT_TYPE_TEX:
		{
			texVert tv;
			memcpy(&tv, &(data[nodeExtOffset]), sizeof(texVert));
			//cout << "texVert - width: " << tv.width << ", height: " << tv.height << ", flags: " << tv.flags << ", data ptr: 0x" << std::hex << tv.dataPtr << endl;
			//cout << std::dec;
			imgHeader.push_back(tv);	//Save header
		}
		break;
		
		case VERT_TYPE_VBO:
		{
			vboVert vv;
			memcpy(&vv, &(data[nodeExtOffset]), sizeof(vboVert));
			
			pieceHeader ph;
			memcpy(&ph, &(data[vv.dataPtr]), sizeof(pieceHeader));
			
			list<piece> pcs;
			for(int i = 0; i < vv.num; i++)
			{
				piece p;
				memcpy(&p, &(data[vv.dataPtr+sizeof(pieceHeader)+i*sizeof(piece)]), sizeof(piece));
				
				//Coordinates are scaled; divide by 10 and hope it works
				//p.topLeft.x = p.topLeft.x;// / 10.0f;
				//p.topLeft.y = p.topLeft.y;// / 10.0f;
				//p.topRight.y = (int)(p.topRight.y * 10.0f + 0.5f);
				//p.topLeft.x = (int)(p.topLeft.x * 10.0f + 0.5f);
				//p.topLeft.y = (int)(p.topLeft.y * 10.0f + 0.5f);
				
				//Store piece
				pcs.push_back(p);
				
				//cout << "Piece: " << p.topLeft.x << "," << p.topLeft.y << " " << p.topLeftUV.x << "," << p.topLeftUV.y << " - " << p.width << ", " << p.height << endl;// << " " << p.bottomLeftUV.x << ", " << p.bottomLeftUV.y << endl;
			}
			imgPieces.push_back(pcs);
		}
		break;
		
		case VERT_TYPE_SEQ:
		{
			frameSeq fs;
			memcpy(&fs, &(data[nodeExtOffset]), sizeof(frameSeq));
			
			//cout << "Frame seq - ID: " << fs.id << ", # frames: " << fs.numFrames << endl;
			iNumFrameSequences++;
			
			list<frame> fsl;
			frameSequences.push_back(fsl);
		}
		break;
		
		case VERT_TYPE_SEQ_FRAME:
		{
			frame f;
			memcpy(&f, &(data[nodeExtOffset]), sizeof(frame));
			
			//cout << "Frame - Number: " << f.frameNo << ", Delay: " << f.delay << endl;
			frameSequences[iNumFrameSequences-1].push_back(f);
		}
		break;
		
		case VERT_TYPE_FRAME:
		{
			iNumFiles++;
			
			//frameVert fv;
			//memcpy(&fv, &(data[nodeExtOffset]), sizeof(frameVert));
			
			//cout << "FrameVert - minx: " << fv.minx << ", maxx: " << fv.maxx << ", miny: " << fv.miny << ", maxy: " << fv.maxy << endl;
			//animSizes.push_back(fv);
		}
		break;
		
		default:
			break;
	}
}

void tabLevel(int level)
{
	for(int i = 0; i < level; i++)
		cout << '\t';
}

//Recursively dig through children of this vertex
void iterateChild(uint8_t* data, dataVert v, int level, uint32_t offset)
{
	//tabLevel(level);
	//cout << "Vert: type: " << v.type << ", num children: " << v.numChildren << endl;
	checkVert(v, data, offset);
	for(int i = 0; i < v.numChildren; i++)	//Base case: number of children = 0
	{
		dataVert vChild;
		childNodePtr cp;
		
		memcpy(&cp, &(data[v.childPtr+(i*sizeof(childNodePtr))]), sizeof(childNodePtr));
		memcpy(&vChild, &(data[cp.offset]), sizeof(dataVert));
		
		iterateChild(data, vChild, level+1, cp.offset);
	}
}

int splitImages(const char* cFilename)
{
	bFileHit = false;
	bRedo = false;
	uint8_t* fileData;
	FILE* fh = fopen( cFilename, "rb" );
	if(fh == NULL)
	{
		cerr << "Unable to open input file " << cFilename << endl;
		return 1;
	}
	fseek(fh, 0, SEEK_END);
	size_t fileSize = ftell(fh);
	fseek(fh, 0, SEEK_SET);
	fileData = new uint8_t[fileSize];
	size_t amt = fread(fileData, fileSize, 1, fh );
	fclose(fh);
	cout << "Splitting images from file " << cFilename << endl;
	
	
	//Figure out what we'll be naming the images
	string sName = cFilename;
	//First off, strip off filename extension
	size_t namepos = sName.find(".anb");
	if(namepos != string::npos)
		sName.erase(namepos);
	//Next, strip off any file path before it
	namepos = sName.rfind('/');
	if(namepos == string::npos)
		namepos = sName.rfind('\\');
	if(namepos != string::npos)
		sName.erase(0, namepos+1);
		
	//Read file header
	anbHeader ah;
	memcpy(&ah, fileData, sizeof(anbHeader));
	
	//Cycle through ANB data pointer tree
	imgPieces.clear();
	imgHeader.clear();
	sCurFileName = sName;
	iNumFiles = 0;
	frameSequences.clear();
	iNumFrameSequences = 0;
	iterateChild(fileData, ah.head, 0, 16);		//Workhorse: Spin through ANB data tree
	
	//Find max anim sizes for each sequence
	animSizes.reserve(imgPieces.size());
	for(int iCurFile = 0; iCurFile < iNumFiles; iCurFile++)
	{
		frameVert fv;
		fv.minx = fv.maxx = fv.miny = fv.maxy = 0;
		animSizes[iCurFile] = fv;
	}
	for(int i = 0; i < iNumFrameSequences; i++)
	{
		//Figure out pieces
		frameVert fv;
		fv.minx = fv.maxx = fv.miny = fv.maxy = 0;
		for(list<frame>::iterator j = frameSequences[i].begin(); j != frameSequences[i].end(); j++)
		{
			for(list<piece>::iterator k = imgPieces[j->frameNo].begin(); k != imgPieces[j->frameNo].end(); k++)
			{
				//Store our maximum values, so we know how large the image is
				if(k->topLeft.x < fv.minx)
					fv.minx = k->topLeft.x;
				if(k->topLeft.y < fv.miny)
					fv.miny = k->topLeft.y;
				if(k->topLeft.x + k->width > fv.maxx)
					fv.maxx = k->topLeft.x + k->width;
				if(k->topLeft.y + k->height > fv.maxy)
					fv.maxy = k->topLeft.y + k->height;
			}
		}
		for(list<frame>::iterator j = frameSequences[i].begin(); j != frameSequences[i].end(); j++)
		{
			if(fv.minx < animSizes[j->frameNo].minx)
				animSizes[j->frameNo].minx = fv.minx;
			if(fv.miny < animSizes[j->frameNo].miny)
				animSizes[j->frameNo].miny = fv.miny;
			if(fv.maxx > animSizes[j->frameNo].maxx)
				animSizes[j->frameNo].maxx = fv.maxx;
			if(fv.maxy > animSizes[j->frameNo].maxy)
				animSizes[j->frameNo].maxy = fv.maxy;
		}
		
	}
	
	//Decompress and piece all images up front
	vector<FIBITMAP*> frameImages;
	for(int i = 0; i < iNumFiles; i++)
		frameImages.push_back(decompressFrame(fileData, i));
	
	//There's a chance that the first header actually DOESN'T have the image; one of the later ones will, so loop through again
	if(bRedo)
	{
		frameImages.clear();
		for(int i = 0; i < iNumFiles; i++)
			frameImages.push_back(decompressFrame(fileData, i));
	}
	
	//Create icon
	if(g_bCreateIcon)
	{
		bool bTemp = g_bGreenBg;
		g_bGreenBg = false;	//We want a transparent bg on our icon
		
		list<frame>::iterator firstSeq = frameSequences[0].begin();
		FIBITMAP* icon = decompressFrame(fileData, firstSeq->frameNo);	//Grab the first image of the first frame sequence
		if(FreeImage_GetWidth(icon) * 2 < iconW && FreeImage_GetHeight(icon) * 2 < iconH)	//Can scale up by a factor of 2 safely
		{
			FIBITMAP* rescaled = FreeImage_Rescale(icon, FreeImage_GetWidth(icon) * 2, FreeImage_GetHeight(icon) * 2, FILTER_BOX);
			FreeImage_Unload(icon);
			icon = rescaled;
		}
		else if(FreeImage_GetWidth(icon) > iconW || FreeImage_GetHeight(icon) > iconH)	//Need to scale down
		{
			FIBITMAP* rescaled = FreeImage_MakeThumbnail(icon, iconH);
			FreeImage_Unload(icon);
			icon = rescaled;
		}
		
		int left = floor((float)(iconW - FreeImage_GetWidth(icon))/2);
		int right = ceil((float)(iconW - FreeImage_GetWidth(icon))/2);
		int top = floor((float)(iconH - FreeImage_GetHeight(icon))/2);
		int bottom = ceil((float)(iconH - FreeImage_GetHeight(icon))/2);
		
		RGBQUAD q = {0,0,0,0};
		FIBITMAP* resized = FreeImage_EnlargeCanvas(icon, left, top, right, bottom, &q, FI_COLOR_IS_RGB_COLOR);	//Resize to 148x125
		FreeImage_Unload(icon);
		icon = resized;
	
		ostringstream oss;
		oss << "output/" << sCurFileName << "_icon.png";
		cout << "Saving " << oss.str() << endl;
		FreeImage_Save(FIF_PNG, icon, oss.str().c_str());
		g_bGreenBg = bTemp;
	}
		
	//Figure out dimensions of final image
	int finalX = offsetX;
	int finalY = offsetY;
	for(int i = 0; i < iNumFrameSequences; i++)
	{
		int animMaxX = offsetX;
		int animMaxY = 0;
		for(list<frame>::iterator j = frameSequences[i].begin(); j != frameSequences[i].end(); j++)
		{
			if(frameImages[j->frameNo] != NULL)
			{
				animMaxX += FreeImage_GetWidth(frameImages[j->frameNo]);
				if(FreeImage_GetHeight(frameImages[j->frameNo]) > animMaxY)
					animMaxY = FreeImage_GetHeight(frameImages[j->frameNo]);
				animMaxX += offsetX;
			}
		}
		if(animMaxX > finalX)
			finalX = animMaxX;
		finalY += offsetY + animMaxY;
	}
	
	//Allocate final image, and piece
	FIBITMAP* finalSheet = FreeImage_Allocate(finalX, finalY, 32);
	RGBQUAD q = {128,128,0,255};
	FreeImage_FillBackground(finalSheet, (const void *)&q);
	
	int curX = offsetX;
	int curY = offsetY;
	//Split each animation up into frame sequences
	for(int i = 0; i < iNumFrameSequences; i++)
	{
		int iCurFrame = 0;
		int animMaxY = 0;
		for(list<frame>::iterator j = frameSequences[i].begin(); j != frameSequences[i].end(); j++)
		{
			if(frameImages[j->frameNo] != NULL)
			{
				FreeImage_Paste(finalSheet, frameImages[j->frameNo], curX, curY, 300);
				curX += offsetX + FreeImage_GetWidth(frameImages[j->frameNo]);
				if(FreeImage_GetHeight(frameImages[j->frameNo]) > animMaxY)
					animMaxY = FreeImage_GetHeight(frameImages[j->frameNo]);
			}
		}
		curX = offsetX;
		curY += animMaxY + offsetY;
	}
	ostringstream oss;
	oss << "output/" << sCurFileName << ".png";
	cout << "Saving " << oss.str() << endl;
	FIBITMAP* res_24;
	if(g_bGreenBg)
	{
		res_24 = FreeImage_ConvertTo24Bits(finalSheet);
		FreeImage_Save(FIF_PNG, res_24, oss.str().c_str());
	}
	else
		FreeImage_Save(FIF_PNG, finalSheet, oss.str().c_str());
	
	//Free our image data
	for(vector<FIBITMAP*>::iterator i = frameImages.begin(); i != frameImages.end(); i++)
		FreeImage_Unload(*i);
	
	FreeImage_Unload(finalSheet);
	if(g_bGreenBg)
		FreeImage_Unload(res_24);
	delete[] fileData;
	
	if(dst)
	{
		free(dst);
		dst = NULL;
	}
	return 0;
}

int main(int argc, char** argv)
{
	g_bGreenBg = true;
	g_bCreateIcon = true;
	FreeImage_Initialise();
#ifdef _WIN32
	CreateDirectory(TEXT("output"), NULL);
#else
	int result = system("mkdir -p output");
#endif
	list<string> sFilenames;
	//Parse commandline
	for(int i = 1; i < argc; i++)
	{
		string s = argv[i];
		sFilenames.push_back(s);
	}
	//Decompress ANB files
	for(list<string>::iterator i = sFilenames.begin(); i != sFilenames.end(); i++)
		splitImages((*i).c_str());
	FreeImage_DeInitialise();
	return 0;
}
