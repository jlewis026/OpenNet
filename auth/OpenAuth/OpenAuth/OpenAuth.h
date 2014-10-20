// OpenAuth.h

#ifndef OpenNet_Auth
#define OpenNet_Auth
#include <stdint.h>
#include <stdlib.h>
extern "C" {
	void* Initialize();
}



//OS-Specific routines
extern "C" {
	//Creates a 20-byte hash
	void* CreateHash();
	void UpdateHash(void* hash, const unsigned char* data, size_t sz);
	void FinalizeHash(void* hash, unsigned char* output);
	bool VerifySignature(unsigned char* data, size_t dlen, unsigned char* signature, size_t slen);
	size_t CreateSignature(const unsigned char* data, size_t dlen, unsigned char* signature);
}
#endif
