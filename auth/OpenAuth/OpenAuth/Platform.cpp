// This is the main DLL file.

#include "stdafx.h"

#include "OpenAuth.h"

#include <sha.h>
#include <rsa.h>
using namespace System;

namespace OpenAuth {

	public ref class AuthenticationProvider
	{

	};
}

class IDisposable {
public:
	virtual ~IDisposable(){};
};
#define CP(val,type)((type*)val)
#define C(val,type)((type)val)
using namespace CryptoPP;
extern "C" {
	void* CreateHash() {
		return new SHA1();
	}
	void UpdateHash(void* hash, const unsigned char* data, size_t sz) {
		CP(hash, SHA1)->Update(data, sz);
	}
	void FinalizeHash(void* hash, unsigned char* output) {
		CP(hash, SHA1)->Final(output);
		delete CP(hash, SHA1);
	}
	bool VerifySignature(unsigned char* data, size_t dlen, unsigned char* signature, size_t slen) {
		RSASSA_PKCS1v15_SHA_Verifier verifier;
		return verifier.VerifyMessage(data, dlen, signature, slen);

	}
	size_t CreateSignature(const unsigned char* data, size_t dlen, unsigned char* signature) {
		RSASSA_PKCS1v15_SHA_Signer signer;
		size_t mlen = signer.MaxSignatureLength();
		bool rst = false;
		if (signature == 0) {
			signature = new unsigned char[mlen];
			rst = true;
		}
		size_t retval = signer.SignMessage(CryptoPP::RandomNumberGenerator(), data, dlen, signature);
		if (rst) {
			delete[] signature;
		}
		return retval;
	}
}