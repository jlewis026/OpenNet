/*
 * Platform.cpp
 *
 *  Created on: May 16, 2014
 *      Author: brian
 */
#include <string>
#include "OpenNet.h"
#include <uuid/uuid.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include "sqlite3.h"
#include <crypto++/modes.h>
#include <crypto++/filters.h>
#include <crypto++/aes.h>
#include <algorithm>
#include <math.h>
using namespace OpenNet;



class AESCryptEngine:public IDisposable {
public:
		CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption encryption;
		CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption decryption;
		AESCryptEngine(unsigned char key[32]) {
			unsigned char iv[16];
			encryption.SetKeyWithIV(key,32,iv,16);
			decryption.SetKeyWithIV(key,32,iv,16);
		}
		void Encrypt(unsigned char* data, unsigned char iv[16],  size_t sz) {
			encryption.Resynchronize(iv,16);
			encryption.ProcessData(data,data,sz);

		}
		void Decrypt(unsigned char* data, unsigned char iv[16], size_t sz) {
			decryption.Resynchronize(iv,16);
			decryption.ProcessData(data,data,sz);

		}
	};

extern "C" {
//These functions must be implemented by the operating system
	//Makes a GUID represented by a file name
	void OS_MkGuid(char output[256]) {
		unsigned char val[16];
		uuid_generate(val);
		memset(output,0,256);
		uuid_unparse(val,output);

	}
	//Parses a GUID
	void OS_ParseGuid(const char* input, unsigned char output[16]) {
		uuid_parse(input,output);
	}
	//Converts a GUID to a string
	void OS_GuidToString(const unsigned char input[16], char output[256]) {
		memset(output,0,256);
		uuid_unparse(input,output);

	}

	using namespace CryptoPP;
	void* OS_AES_GetKeyHandle(unsigned char key[32]) {

		AESCryptEngine* retval = new AESCryptEngine(key);
		return retval;
	}
	void OS_Aes_Encrypt(void* keyHandle, unsigned char* data, size_t sz,unsigned char iv[16]) {
		auto bot = (AESCryptEngine*)keyHandle;
		bot->Encrypt(data,iv,sz);

	}
	void OS_Aes_Decrypt(void* keyHandle, unsigned char* data, size_t sz, unsigned char iv[16]) {
		auto bot = (AESCryptEngine*)keyHandle;
		bot->Decrypt(data,iv,sz);

	}
}
