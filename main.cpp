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

/*FIBITMAP* PieceImage(uint8_t* imgData, list<piece> pieces, Vec2 maxul, Vec2 maxbr, texVert th)
{
	Vec2 OutputSize;
	Vec2 CenterPos;
	OutputSize.x = -maxul.x + maxbr.x;
	OutputSize.y = maxul.y - maxbr.y;
	CenterPos.x = -maxul.x;
	CenterPos.y = maxul.y;
	OutputSize.x = uint32_t(OutputSize.x);
	OutputSize.y = uint32_t(OutputSize.y);

	//My math seems off, so rather than solving the problem, create larger than needed then crop. Hooray!
	FIBITMAP* result = FreeImage_Allocate(OutputSize.x+6, OutputSize.y+6, 32);

	//Create image from this set of pixels
	FIBITMAP* curImg = imageFromPixels(imgData, th.width, th.height);

	//Patch image together from pieces
	for(list<piece>::iterator lpi = pieces.begin(); lpi != pieces.end(); lpi++)
	{
		FIBITMAP* imgPiece = FreeImage_Copy(curImg, 
											(int)((lpi->topLeftUV.x) * th.width + 0.5), (int)((lpi->topLeftUV.y) * th.height + 0.5), 
											(int)((lpi->bottomRightUV.x) * th.width + 0.5), (int)((lpi->bottomRightUV.y) * th.height + 0.5));
		
		//Since pasting doesn't allow you to post an image onto a particular position of another, do that by hand
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
		}
		
		FreeImage_Unload(imgPiece);
	}
	FreeImage_Unload(curImg);
	
	//Crop edges from final image
	FIBITMAP* cropped = FreeImage_Copy(result, 3, 3, FreeImage_GetWidth(result)-2, FreeImage_GetHeight(result)-2);
	FreeImage_Unload(result);
	
	return cropped;
}*/

list<piece> imgPieces;
texVert imgHeader;
int iCurFile;
string sCurFileName;

void saveCurImage(uint8_t* data)
{
	if(!imgPieces.size()) return;	//Sanity check if first image
	
	//Decompress WFLZ data - one pass now, no chunks
	const uint32_t decompressedSize = wfLZ_GetDecompressedSize(&(data[imgHeader.dataPtr+sizeof(texHeader)]));
	uint8_t* dst = (uint8_t*)malloc(decompressedSize);
	wfLZ_Decompress(&(data[imgHeader.dataPtr+sizeof(texHeader)]), dst);
	
	ostringstream oss;
	oss << "output/" << sCurFileName << '/' << setfill('0') << setw(3) << iCurFile+1 << ".png";
	cout << "Saving " << oss.str() << endl;
	
	//TODO: Piece together
	FIBITMAP* result = imageFromPixels(dst, imgHeader.width, imgHeader.height);
	
	FreeImage_Save(FIF_PNG, result, oss.str().c_str());
	
	//FILE* fOut = fopen(oss.str().c_str(), "wb");
	//fwrite(dst, sizeof(uint8_t), decompressedSize, fOut);
	
	//Free allocated memory
	FreeImage_Unload(result);
	free(dst);
	
	iCurFile++;
	imgPieces.clear();	//New frame, new pieces
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
			imgHeader = tv;	//Save header
			//cout << "texVert - width: " << tv.width << ", height: " << tv.height << ", flags: " << tv.flags << ", data ptr: 0x" << std::hex << tv.dataPtr << endl;
			//cout << std::dec;
		}
		break;
		
		case VERT_TYPE_VBO:
		{
			vboVert vv;
			memcpy(&vv, &(data[nodeExtOffset]), sizeof(vboVert));
			//cout << "vboVert - num: " << vv.num << ", flags: " << vv.flags << ", offset: 0x" << std::hex << vv.dataPtr << endl;
			//cout << std::dec;
			
			pieceHeader ph;
			memcpy(&ph, &(data[vv.dataPtr]), sizeof(pieceHeader));
			
			for(int i = 0; i < vv.num; i++)
			{
				piece p;
				memcpy(&p, &(data[vv.dataPtr+sizeof(pieceHeader)+i*sizeof(piece)]), sizeof(piece));
				
				//Store piece
				imgPieces.push_back(p);
			}
		}
		break;
		
		case VERT_TYPE_FRAME:
		{
			saveCurImage(data);	//Save our last image (works because we're traversing depth-first)
		}
		break;
		
		default:
			//cout << endl;
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
	//tabLevel(level);
	checkVert(v, data, offset);
	//if(v.numChildren)
	//{
	//	tabLevel(level);
	//	cout << "Children:" << endl;
	//}
	for(int i = 0; i < v.numChildren; i++)	//Base case: number of children = 0
	{
		dataVert vChild;
		childNodePtr cp;
		
		memcpy(&cp, &(data[v.childPtr+(i*sizeof(childNodePtr))]), sizeof(childNodePtr));
		memcpy(&vChild, &(data[cp.offset]), sizeof(dataVert));
		
		iterateChild(data, vChild, level+1, cp.offset);
	}
	
	//tabLevel(level);
	//cout << "end Vert" << endl;
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
	iCurFile = 0;
	imgPieces.clear();
	sCurFileName = sName;
	iterateChild(fileData, ah.head, 0, 16);		//Workhorse: Spin through ANB data tree
	saveCurImage(fileData);	//Save our last image left
	
	//Parse through, splitting out before each WFLZ header
	/*int iCurFile = 0;
	uint64_t startPos = 0;
	for(uint64_t i = 0; i < fileSize; i++)	//Definitely not the fastest way to do it... but I don't care
	{
		if(memcmp ( &(fileData[i]), "WFLZ", 4 ) == 0)	//Found another file
		{
			uint64_t headerPos = i - sizeof(texHeader);
			texHeader th;
			memcpy(&th, &(fileData[headerPos]), sizeof(texHeader));
			
			//if(th.type != TEXTURE_TYPE_RAW)
			//	cout << "Warning: TexHeader type " << th.type << endl;
			
			//cout << "WFLZ header " << iCurFile+1 << " found. TexHeader type: " << th.type << ", width: " << th.width << ", height: " << th.height << ", bpp: " << th.bpp << ", decompSize: " << th.decompSize << ", compressedSize: " << th.compressedSize << endl;
			
			//Decompress WFLZ data - one pass now, no chunks
			uint32_t* chunk = NULL;
			const uint32_t decompressedSize = wfLZ_GetDecompressedSize(&(fileData[i]));
			uint8_t* dst = (uint8_t*)malloc(decompressedSize);
			wfLZ_Decompress(&(fileData)[i], dst);
			
			ostringstream oss;
			oss << "output/" << sName << '/' << setfill('0') << setw(3) << iCurFile+1 << ".png";
			cout << "Saving " << oss.str() << endl;
			
			FIBITMAP* result = imageFromPixels(dst, th.width, th.height);
			
			FreeImage_Save(FIF_PNG, result, oss.str().c_str());
			
			//FILE* fOut = fopen(oss.str().c_str(), "wb");
			//fwrite(dst, sizeof(uint8_t), decompressedSize, fOut);
			
			//Free allocated memory
			FreeImage_Unload(result);
			free(dst);
			//fclose(fOut);
			//free(dest_final);
			//free(color);
			//free(mul);
			
			iCurFile++;
		}
	}*/
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
		if(s == "-0")
			g_DecompressFlags = 0;
		else if(s == "-1")
			g_DecompressFlags = 1;
		else if(s == "-2")
			g_DecompressFlags = 2;
		else if(s == "-3")
			g_DecompressFlags = 3;
		else if(s == "-4")
			g_DecompressFlags = 4;
		else if(s == "-5")
			g_DecompressFlags = 5;
		else if(s == "-6")
			g_DecompressFlags = 6;
		else if(s == "-separate")
			g_bSeparate = true;
		else if(s == "-col-only")
		{
			g_bColOnly = true;
			g_bMulOnly = false;
			g_bSeparate = true;
		}
		else if(s == "-mul-only")
		{
			g_bMulOnly = true;
			g_bColOnly = false;
			g_bSeparate = true;
		}
		else if(s == "-nopiece")
			g_bPieceTogether = false;
		else if(s == "-piece")
			g_bPieceTogether = true;
		else
			sFilenames.push_back(s);
	}
	//Decompress ANB files
	for(list<string>::iterator i = sFilenames.begin(); i != sFilenames.end(); i++)
		splitImages((*i).c_str());
	FreeImage_DeInitialise();
	return 0;
}
