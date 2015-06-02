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

int g_DecompressFlags;
bool g_bSeparate;
bool g_bPieceTogether;
bool g_bColOnly;
bool g_bMulOnly;

int offsetX = 2;
int offsetY = 2;


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
	Vec2 topRight;
	Vec2 bottomLeft;
	Vec2 topRightUV;
	Vec2 bottomLeftUV;
} piece;

//Helper struct for filling out a FIBITMAP
typedef struct
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
} pixel;



vector<list<piece> > imgPieces;
vector<texVert> imgHeader;
int iNumFiles;
string sCurFileName;
Vec2 maxul;
Vec2 maxbr;
int iNumFrameSequences;
vector<list<frame> > frameSequences;

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
				pixel[FI_RGBA_RED] = imgData[curPos++];
				pixel[FI_RGBA_GREEN] = imgData[curPos++];
				pixel[FI_RGBA_BLUE] = imgData[curPos++];
				pixel[FI_RGBA_ALPHA] = imgData[curPos++];
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
	OutputSize.y = maxul.y - maxbr.y;
	CenterPos.x = -maxul.x;
	CenterPos.y = maxul.y;
	OutputSize.x = uint32_t(OutputSize.x);
	OutputSize.y = uint32_t(OutputSize.y);

	//My math seems off, so rather than solving the problem, create larger than needed, then crop. Hooray!
	FIBITMAP* result = FreeImage_Allocate(OutputSize.x+6, OutputSize.y+6, 32);

	//Create image from this set of pixels
	FIBITMAP* curImg = imageFromPixels(imgData, th.width, th.height);

	//Patch image together from pieces
	for(list<piece>::iterator lpi = pieces.begin(); lpi != pieces.end(); lpi++)
	{
		FIBITMAP* imgPiece = FreeImage_Copy(curImg, 
											(int)((lpi->bottomLeftUV.x) * th.width + 0.5), (int)((lpi->topRightUV.y) * th.height + 0.5), 
											(int)((lpi->topRightUV.x) * th.width + 0.5), (int)((lpi->bottomLeftUV.y) * th.height + 0.5));
		
		//Since pasting doesn't allow you to post an image onto a particular position of another, do that by hand
		int curPos = 0;
		int srcW = FreeImage_GetWidth(imgPiece);
		int srcH = FreeImage_GetHeight(imgPiece);
		unsigned pitch = FreeImage_GetPitch(imgPiece);
		unsigned destpitch = FreeImage_GetPitch(result);
		BYTE* bits = (BYTE*)FreeImage_GetBits(imgPiece);
		BYTE* destBits = (BYTE*)FreeImage_GetBits(result);
		Vec2 DestPos = CenterPos;
		DestPos.x += lpi->bottomLeft.x;
		DestPos.y = OutputSize.y - srcH;
		DestPos.y -= CenterPos.y;
		DestPos.y += lpi->topRight.y;
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
		}
		
		FreeImage_Unload(imgPiece);
	}
	FreeImage_Unload(curImg);
	
	//Crop edges from final image
	FIBITMAP* cropped = FreeImage_Copy(result, 3, 3, FreeImage_GetWidth(result)-2, FreeImage_GetHeight(result)-2);
	FreeImage_Unload(result);
	
	return cropped;
}

FIBITMAP* decompressFrame(uint8_t* data, int iFile)
{
	if(imgPieces.size() <= iFile || imgHeader.size() <= iFile) return NULL;	//Skip if we don't have enough headers...
	if(imgHeader[iFile].width <= 0 || imgHeader[iFile].height <= 0 || imgHeader[iFile].dataPtr <= 0) return NULL;	//Skip if it's not a legit header

	//Decompress WFLZ data - one pass now, no chunks
	const uint32_t decompressedSize = wfLZ_GetDecompressedSize(&(data[imgHeader[iFile].dataPtr+sizeof(texHeader)]));
	uint8_t* dst = (uint8_t*)malloc(decompressedSize);
	wfLZ_Decompress(&(data[imgHeader[iFile].dataPtr+sizeof(texHeader)]), dst);
	
	//ostringstream oss;
	//oss << "output/" << sCurFileName << '/' << setfill('0') << setw(3) << iFile+1 << ".png";
	//cout << "Saving " << oss.str() << endl;
	
	//Piece together
	FIBITMAP* result = PieceImage(dst, imgPieces[iFile], maxul, maxbr, imgHeader[iFile]);//imageFromPixels(dst, imgHeader.width, imgHeader.height);
	
	//FreeImage_Save(FIF_PNG, result, oss.str().c_str());
	
	//Free allocated memory
	//FreeImage_Unload(result);
	free(dst);
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
				
				//Coordinates are scaled; multiply by 10 and hope it works
				p.topRight.x = (int)(p.topRight.x * 10.0f + 0.5f);
				p.topRight.y = (int)(p.topRight.y * 10.0f + 0.5f);
				p.bottomLeft.x = (int)(p.bottomLeft.x * 10.0f + 0.5f);
				p.bottomLeft.y = (int)(p.bottomLeft.y * 10.0f + 0.5f);
				
				//Store piece
				pcs.push_back(p);
				
				//cout << "Piece: " << p.topRight.x << ", " << p.topRight.y << " " << p.bottomLeft.x << ", " << p.bottomLeft.y << " - " << p.topRightUV.x << ", " << p.topRightUV.y << " " << p.bottomLeftUV.x << ", " << p.bottomLeftUV.y << endl;
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
			iNumFiles++;
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
		
	//Create the folder we'll be saving into
	#ifdef _WIN32
		string sOutDir = "output/";
		sOutDir += sName;
		CreateDirectory(TEXT(sOutDir.c_str()), NULL);
	#else
		#error Do something here to create folder
	#endif
		
	//Read file header
	anbHeader ah;
	memcpy(&ah, fileData, sizeof(anbHeader));
	
	//Cycle through ANB data pointer tree
	imgPieces.clear();
	imgHeader.clear();
	sCurFileName = sName;
	maxul.x = maxul.y = maxbr.x = maxbr.y = 0;
	iNumFiles = 0;
	frameSequences.clear();
	iNumFrameSequences = 0;
	iterateChild(fileData, ah.head, 0, 16);		//Workhorse: Spin through ANB data tree
	
	for(int iCurFile = 0; iCurFile < iNumFiles; iCurFile++)
	{
		//Figure out pieces
		for(list<piece>::iterator i = imgPieces[iCurFile].begin(); i != imgPieces[iCurFile].end(); i++)
		{
			//Store our maximum values, so we know how large the image is
			if(i->bottomLeft.x < maxul.x)
				maxul.x = i->bottomLeft.x;
			if(i->topRight.y > maxul.y)
				maxul.y = i->topRight.y;
			if(i->topRight.x > maxbr.x)
				maxbr.x = i->topRight.x;
			if(i->bottomLeft.y < maxbr.y)
				maxbr.y = i->bottomLeft.y;
		}
	}
	
	//Decompress and piece all images up front
	vector<FIBITMAP*> frameImages;
	for(int i = 0; i < iNumFiles; i++)
		frameImages.push_back(decompressFrame(fileData, i));
		
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
	//cout << "Final image: " << finalX << ", " << finalY << endl;
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
	oss << "output/" << sCurFileName << "_sheet.png";
	cout << "Saving " << oss.str() << endl;
	FreeImage_Save(FIF_PNG, finalSheet, oss.str().c_str());
	
	//Free our image data
	for(vector<FIBITMAP*>::iterator i = frameImages.begin(); i != frameImages.end(); i++)
		FreeImage_Unload(*i);
	
	FreeImage_Unload(finalSheet);
	delete[] fileData;
	return 0;
}

int main(int argc, char** argv)
{
	g_DecompressFlags = -1;
	g_bSeparate = false;
	g_bPieceTogether = true;
	g_bColOnly = g_bMulOnly = false;
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
