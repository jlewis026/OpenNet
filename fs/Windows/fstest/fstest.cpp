// fstest.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <OpenNet.h>
#include <algorithm>
unsigned char bigdata[1024 * 1024 * 50];

int _tmain(int argc, _TCHAR* argv[])
{
	
	unsigned char enckey[32];
	memset(enckey, 0, 32);

	//Mount the Windows filesystem
	Filesystem winfs;
	OS_GetFilesystem(&winfs);
	//Mount our custom filesystem on top of the Windows filesystem
	Filesystem distfs;
	//Encryption key will be all zeroes for test run
	
	OpenNet_MountFS(&winfs, &distfs, (char*)enckey);
	OpenNet::ABI_Filesystem fs(distfs);
	if (!fs.Exists("testfile")) {
		fs.Mknod("testfile", true);
		OpenNet::Stream* ostr = fs.GetStream("testfile");
		ostr->Write(0, bigdata);
		delete ostr;
	}
	else {
		OpenNet::Stream* istr = fs.GetStream("testfile");
		int dataprocessed = istr->Read(0, bigdata);
		delete istr;
	}
	return 0;
}

