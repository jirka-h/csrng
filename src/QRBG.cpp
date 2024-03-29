/*
QRBG Service API - see http://random.irb.hr/
To retrieve the random data from this service you will need to 
go to the http://random.irb.hr/ and register there first

Copyright (C) 2007 Radomir Stevanovic and Rudjer Boskovic Institute.
Copyright (C) 2011, 2012 Jirka Hladky

This file is part of CSRNG http://code.google.com/p/csrng/

CSRNG is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

CSRNG is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with CSRNG.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "QRBG.h"

#include <sys/types.h>
#include <string.h>		// memcpy


#ifdef PLATFORM_WIN
	// windows includes
#	define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

#	include <winsock2.h>
#	include <WS2tcpip.h>
#	include <windows.h>
#	include <errno.h>

#	define GetLastSocketError()	WSAGetLastError()

#	pragma comment(lib, "ws2_32.lib")	// link with winsock2 library
#endif

#ifdef PLATFORM_LINUX
	// linux includes
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h> 
#	include <netdb.h>
#	include <unistd.h>

#	include <errno.h>
	extern int errno;

#	define GetLastSocketError() errno

#endif

#define ASSERT(assertion)

// WinSock's recv() fails if called with too large buffer size, so
// limit maximum amount of data that could be received in
// one recv() call.
//#define INTERNAL_SOCKET_MAX_BUFFER	65536
#define INTERNAL_SOCKET_MAX_BUFFER	1048576

const char* QRBG::ServerResponseDescription[] = {
	"OK",
	"Service was shutting down",
	"Server was/is experiencing internal errors",
	"Service said we have requested some unsupported operation",
	"Service said we sent an ill-formed request packet",
	"Service said we were sending our request too slow",
	"Authentication failed",
	"User quota exceeded"
};

const char* QRBG::ServerResponseRemedy[] = {
	"None",
	"Try again later",
	"Try again later",
	"Upgrade your client software",
	"Upgrade your client software",
	"Check your network connection",
	"Check your login credentials",
	"Try again later, or contact Service admin to increase your quota(s)"
};


// Initializes class data members, initializes network subsystem.
// Throws exceptions upon failure (memory / winsock).
QRBG::QRBG(size_t cacheSize /*= DEFAULT_CACHE_SIZE*/) /* throw(NetworkSubsystemError, bad_alloc) */
: port(0), hSocket(-1)
, inBuffer(NULL)
, inBufferSize(cacheSize)
, outBuffer(NULL)
, outBufferSize(4096) /* WARNING: 'outBuffer' MUST be large enough to store whole request header before sending! */
, inBufferNextElemIdx(inBufferSize) {
	*szHostname = *szUsername = *szPassword = 0;

	// initialize socket subsystem
#ifdef PLATFORM_WIN
	// initialize winsock

	WORD wVersionRequested;
	WSADATA wsaData;

	wVersionRequested = MAKEWORD(2, 2);

	int err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		// we could not find a usable WinSock DLL (Winsock v2.2 was requested/required)
		throw NetworkSubsystemError();
	}

	// Confirm that the WinSock DLL supports 2.2.
	// Note: if the DLL supports versions greater than 2.2 in addition to 2.2, 
	// it will still return 2.2 in wVersion since that is the version we requested.
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
		// we could not find a usable WinSock DLL
		WSACleanup( );
		throw NetworkSubsystemError();
	}
#elif defined(PLATFORM_LINUX)
	// there's no need for linux sockets to init
#endif

	// if memory allocation fails, propagate exception to the caller
	inBuffer = new byte[inBufferSize];
	outBuffer = new byte[outBufferSize];
}

QRBG::~QRBG() {
	delete[] outBuffer;
	delete[] inBuffer;
}

// Re-initializes the cache buffer.
// New cache size (in bytes) must be AT LEAST 8 bytes (to accommodate the largest type - double),
// however this function shall not allow new cache size to be less then MINIMAL_CACHE_SIZE bytes.
// Returns: success?
bool QRBG::defineCache(size_t cacheSize /* = DEFAULT_CACHE_SIZE */) {
	if (cacheSize < MINIMAL_CACHE_SIZE)
		return false;

	// try to alloc new cache buffer..
	byte* newBuffer;
	try {
		newBuffer = new byte[cacheSize];
	} catch (...) {
		return false;
	}

	// ok, now delete old one and start using newly allocated one!
	delete[] inBuffer;
	inBuffer = newBuffer;
	inBufferSize = cacheSize;
	inBufferNextElemIdx = inBufferSize;

	return true;
}

//////////////////////////////////////////////////////////////////////////
//
// private (worker) methods
//

// Connects to service server and on success, stores connected socket handle
// in private class member.
// Upon failure, exception is thrown.
void QRBG::Connect() throw(ConnectError) {
	if (hSocket != -1) {
		// we're already connected
		return;
	}

	if (!*szHostname || !port) {
		// server address is not defined
		throw ConnectError();
	}

	int hsock = static_cast<int>( socket(AF_INET, SOCK_STREAM, 0) );
	if (hsock == -1) {
		// failed to create socket
		throw ConnectError();
	}

	// try to resolve 'hostname', if we fail, assume 'hostname' as an IP address
	struct sockaddr_in addr;
	struct hostent *hent = gethostbyname(szHostname);
	if (hent == NULL)
		addr.sin_addr.s_addr = inet_addr(szHostname);
	else
		memcpy(&addr.sin_addr, hent->h_addr, hent->h_length);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (connect(hsock, (struct sockaddr *)&addr, sizeof(addr))) {
		// failed to connect
		throw ConnectError();
	}

	hSocket = hsock;
}

// Closes connection with service server (if connection was ever established)
void QRBG::Close() throw() {
	// disallow further sends and receives..
	shutdown(hSocket, 2);

	// delete socket descriptor
#if WIN32
	closesocket(hSocket);
#else
	close(hSocket);
#endif

	// for future checks if socket is closed
	hSocket = -1;
}

// Fills the 'buffer' with maximum of 'count' bytes. Actual number of bytes copied into the buffer
// is returned.
// If local cache buffer doesn't contain enough bytes, then a request to the service for 'count' bytes is sent, 
// (blocking while receiving response), and the 'buffer' is filled with the returned data (random bytes).
// Otherwise, the 'buffer' is filled with locally cached data.
// If user requested less then cache size, the cache is refilled and data is returned from the cache.
// Upon failure, exceptions are thrown.
// Returns: count of bytes (received) copied into the supplied buffer.
size_t QRBG::AcquireBytes(byte* buffer, size_t count) throw(ConnectError, CommunicationError, ServiceDenied) {

	// timer start
#	if defined(PLATFORM_WIN)
		timeStart = GetTickCount();
#	elif defined(PLATFORM_LINUX)
		gettimeofday(&timeStart, NULL);
#	endif

	// actual data acquisition
	size_t nCopied = 0;
	if (count <= inBufferSize) {
		EnsureCachedEnough(count);
		nCopied = AcquireBytesFromCache(buffer, count);
	} else {
		nCopied = AcquireBytesFromService(buffer, count);
	}

	// timer end
#	if defined(PLATFORM_WIN)
		timeEnd = GetTickCount();
#	elif defined(PLATFORM_LINUX)
		gettimeofday(&timeEnd, NULL);
#	endif

	return nCopied;
}


// Fills the 'buffer' with exactly 'count' bytes, explicitly from the local cache.
// Number of bytes copied into the buffer is returned ('count' on success, '0' on failure).
size_t QRBG::AcquireBytesFromCache(byte* buffer, size_t count) throw() {
	// fill the buffer from cache (if anyway possible)..
	if (IsCachedEnough(count)) {
		memcpy(buffer, inBuffer + inBufferNextElemIdx, count);
		inBufferNextElemIdx += count;
		return count;
	}
	// ..or fail
	return 0;
}

// Fills the 'buffer' with maximum of 'count' bytes, explicitly from the remote QRBG Service.
// Actual number of bytes copied into the buffer is returned.
// Upon failure, exceptions are thrown.
// Returns: count of bytes (received) copied into the supplied buffer.
size_t QRBG::AcquireBytesFromService(byte* buffer, size_t count) throw(ConnectError, CommunicationError, ServiceDenied) {
	// connect to the service server,
	// propagate exception to the caller
	Connect();

	//
	// prepare and send the request
	//

	// NOTE: we're using plain authentication.

/*
	Client first (and last) packet:
	Size [B]		Content
	--------------	--------------------------------------------------------
	1				operation, from OperationCodes enum
	if operation == GET_DATA_AUTH_PLAIN, then:
	2				content size (= 1 + username_len + 1 + password_len + 4)
	1				username_len (must be > 0 and <= 100)
	username_len	username (NOT zero padded!)
	1				password_len (must be > 0 and <= 100)
	password_len	password in plain 8-bit ascii text (NOT zero padded!)
	4				bytes of data requested

	Server first (and last) packet:
	Size [B]		Content
	--------------	--------------------------------------------------------
	1				response, from ServerResponseCodes enum
	1				response details - reason, from RefusalReasonCodes
	4				data_len, bytes of data that follow
	data_len		data
*/

	// header structure looks like this:
	// 	struct tClientHeader {
	// 		uint8	eOperation;		// MUST BE eOperation == GET_DATA_AUTH_PLAIN for struct remainder to hold
	// 		uint16	cbContentSize;
	// 		uint8	cbUsername;
	// 		char	szUsername[cbUsername];
	// 		uint8	cbPassword;
	// 		char	szPassword[cbPassword];
	// 		uint32	cbRequested;
	// 	};
	// however, two issues obstruct direct structure usage:
	//	1) we don't know username/password length apriori
	//	2) we must convert all numeric values to network order (big endian)
	// so, we'll fill output buffer byte-by-byte...

	uint8 eOperation = GET_DATA_AUTH_PLAIN;
	uint8 cbUsername = static_cast<uint8>( strlen(szUsername) );
	uint8 cbPassword = static_cast<uint8>( strlen(szPassword) );
	uint32 cbRequested = static_cast<uint32>( count );
	uint16 cbContentSize = sizeof(cbUsername) + cbUsername + sizeof(cbPassword) + cbPassword + sizeof(cbRequested);

	ssize_t bytesToSend = sizeof(eOperation) + sizeof(cbContentSize) + cbContentSize;

	ASSERT(outBufferSize >= bytesToSend);
	byte* pRequestBuffer = outBuffer;

	*(uint8*)pRequestBuffer = eOperation, pRequestBuffer += sizeof(eOperation);
	*(uint16*)pRequestBuffer = htons(cbContentSize), pRequestBuffer += sizeof(cbContentSize);
	*(uint8*)pRequestBuffer = cbUsername, pRequestBuffer += sizeof(cbUsername);
	memcpy(pRequestBuffer, szUsername, cbUsername), pRequestBuffer += cbUsername;
	*(uint8*)pRequestBuffer = cbPassword, pRequestBuffer += sizeof(cbPassword);
	memcpy(pRequestBuffer, szPassword, cbPassword), pRequestBuffer += cbPassword;
	*(uint32*)pRequestBuffer = htonl(cbRequested), pRequestBuffer += sizeof(cbRequested);

	ssize_t ret = send(hSocket, (const char*)outBuffer, bytesToSend, 0);
	if (ret == -1) {
		// failed to send data request to the server
		Close();
		throw CommunicationError();
	}
	if (ret != bytesToSend) {
		// failed to send complete data request to the server
		Close();
		throw CommunicationError();
	}

	//
	// receive header (assuming GET_DATA_AUTH_PLAIN, as we requested)
	//

	// server response header structure looks like this:
	// 	struct tServerHeader {
	// 		uint8 response;		// actually from enum ServerResponseCodes
	// 		uint8 reason;		// actually from enum RefusalReasonCodes
	// 		uint32 cbDataLen;	// should be equal to cbRequested, but we should not count on it!
	// 	};
	// however, to avoid packing and memory aligning portability issues,
	// we'll read input buffer byte-by-byte...
	ServerResponseCodes eResponse;
	RefusalReasonCodes eReason;
	uint32 cbDataLen = 0;

	const uint32 bytesHeader = sizeof(uint8) + sizeof(uint8) + sizeof(uint32);
	byte header[bytesHeader];

	uint32 bytesReceived = 0;
	uint32 bytesToReceiveTotal = bytesHeader;
	uint32 bytesToReceiveNow = 0;

	// receive header
	while ( (bytesToReceiveNow = bytesToReceiveTotal - bytesReceived) > 0 ) {

		int ret = recv(hSocket, (char*)(header + bytesReceived), bytesToReceiveNow, 0);

		if (ret != -1) {
			if (ret > 0) {
				// data received
				bytesReceived += ret;

				// parse the server response
				if (bytesReceived >= 2*sizeof(uint8)) {
					eResponse = (ServerResponseCodes) header[0];
					eReason = (RefusalReasonCodes) header[1];

					// process server response...
					if (eResponse != OK) {
						Close();
						throw ServiceDenied(eResponse, eReason);
					}

					if (bytesReceived >= bytesToReceiveTotal) {
						cbDataLen = ntohl( *((u_long*)(header + 2*sizeof(uint8))) );
					}
				}

			} else {
				// recv() returns 0 if connection was closed by server
				Close();
				throw CommunicationError();
			}

		} else {
			int nErr = GetLastSocketError();
			if (nErr == EAGAIN) {
				// wait a little bit, and try again
			} else {
				// some socket(network) error occurred; 
				// it doesn't matter what it is, declare failure!
				Close();
				throw CommunicationError();
			}
		}
	}


	//
	// receive data
	//

	bytesReceived = 0;
	bytesToReceiveTotal = cbDataLen;

	while ( (bytesToReceiveNow = bytesToReceiveTotal - bytesReceived) > 0 ) {
		// limit to maximal socket buffer size used
		bytesToReceiveNow = bytesToReceiveNow < INTERNAL_SOCKET_MAX_BUFFER ? 
							bytesToReceiveNow : INTERNAL_SOCKET_MAX_BUFFER;

		int ret = recv(hSocket, (char*)(buffer + bytesReceived), bytesToReceiveNow, 0);

		if (ret != -1) {
			if (ret > 0) {
				// data received
				bytesReceived += ret;
			} else {
				// recv() returns 0 if connection was closed by server
				Close();
				throw CommunicationError();
			}

		} else {
			int nErr = GetLastSocketError();
			if (nErr == EAGAIN) {
				// wait a little bit, and try again
			} else {
				// some socket(network) error occurred; 
				// it doesn't matter what it is, declare failure!
				Close();
				throw CommunicationError();
			}
		}
	}

	Close();

	// we succeeded.
	return bytesReceived;
}

// Tests if cache buffer contains at least 'size' bytes.
// Returns: 'true' if it does, and 'false' otherwise.
bool QRBG::IsCachedEnough(size_t size) throw() {
	return inBufferNextElemIdx + size <= inBufferSize;
}

// Ensures that input buffer (local data cache) contains at least 'size' elements.
// In other words, it refills the 'inBuffer' (local data cache) if it doesn't contain at 
// least 'size' bytes of data.
//
// Throws an exception, as indication of failure, in two cases:
//  1) we failed to acquire bytes from service
//  2) we failed to acquire EXACTLY enough bytes to fill WHOLE input buffer
//
void QRBG::EnsureCachedEnough(size_t size) throw(ConnectError, CommunicationError, ServiceDenied) {
	// timer reset
	timeEnd = timeStart;

	if (IsCachedEnough(size))
		return;

	try {
		if (AcquireBytesFromService(inBuffer, inBufferSize) != inBufferSize) 
			throw CommunicationError();
		inBufferNextElemIdx = 0;
	} catch (...) {
		inBufferNextElemIdx = inBufferSize;	// since inBuffer may now be corrupted
		throw;
	}
}


//////////////////////////////////////////////////////////////////////////
//
// info methods
//

#ifdef PLATFORM_WIN
// windows version

// returns the duration (in sec) of the last AcquireBytes(..)
double QRBG::getLastDownloadDuration() {
	return (timeEnd - timeStart) / 1000.0;
}

#elif defined(PLATFORM_LINUX)
// linux version

double QRBG::getLastDownloadDuration() {
	return (timeEnd.tv_sec - timeStart.tv_sec) + 1e-6 * (timeEnd.tv_usec - timeStart.tv_usec);
}

#endif


//////////////////////////////////////////////////////////////////////////
//
// public (interface) methods
//

void QRBG::defineServer(const char* qrbgAddress, unsigned int qrbgPort) throw(InvalidArgumentError) {
	// check parameters
	int len;
	for (len = 0; len <= HOSTNAME_MAXLEN && qrbgAddress[len]; len++);
	if (len > HOSTNAME_MAXLEN) 
		throw InvalidArgumentError();

	if (qrbgPort > 0xFFFF)
		throw InvalidArgumentError();

	// save server settings
	strncpy(szHostname, qrbgAddress, HOSTNAME_MAXLEN);
	port = qrbgPort;
}

void QRBG::defineUser(const char* qrbgUsername, const char* qrbgPassword) throw(InvalidArgumentError) {
	// check parameters
	int len;

	for (len = 0; len <= USERNAME_MAXLEN && qrbgUsername[len]; len++);
	if (len > USERNAME_MAXLEN) 
		throw InvalidArgumentError();

	for (len = 0; len <= PASSWORD_MAXLEN && qrbgPassword[len]; len++);
	if (len > PASSWORD_MAXLEN) 
		throw InvalidArgumentError();

	// save user authentication records
	strncpy(szUsername, qrbgUsername, USERNAME_MAXLEN);
	strncpy(szPassword, qrbgPassword, PASSWORD_MAXLEN);
}

// for future use:
void QRBG::defineUser(const char* qrbgCertificateStore) throw(InvalidArgumentError) {
	throw InvalidArgumentError();
}

//
// integer getter methods
//

/*
// old, faster, non-timed, method
#define IMPLEMENT_QRBG_GETTER(NAME, TYPE) \
	TYPE QRBG::NAME() throw(ConnectError, CommunicationError, ServiceDenied) { \
		EnsureCachedEnough(sizeof(TYPE)); \
		TYPE result = *((TYPE*)(inBuffer + inBufferNextElemIdx)); \
		inBufferNextElemIdx += sizeof(TYPE); \
		return result; \
	}
*/

#define IMPLEMENT_QRBG_GETTER(NAME, TYPE) \
	TYPE QRBG::NAME() throw(ConnectError, CommunicationError, ServiceDenied) { \
		TYPE result = 0xCC; \
		AcquireBytes((byte*)&result, sizeof(TYPE)); \
		return result; \
	}

// fills 'buffer' array of 'TYPE' elements with 'count' elements 
// and returns number of elements filled
#define IMPLEMENT_QRBG_ARRAY_GETTER(NAME, TYPE) \
	size_t QRBG::NAME(TYPE* buffer, size_t count) throw(ConnectError, CommunicationError, ServiceDenied) { \
		return AcquireBytes((byte*)buffer, sizeof(TYPE)*count) / sizeof(TYPE); \
	}

IMPLEMENT_QRBG_GETTER(getByte, byte)
IMPLEMENT_QRBG_GETTER(getInt, int)
IMPLEMENT_QRBG_GETTER(getLongInt, long)

IMPLEMENT_QRBG_ARRAY_GETTER(getBytes, byte)
IMPLEMENT_QRBG_ARRAY_GETTER(getInts, int)
IMPLEMENT_QRBG_ARRAY_GETTER(getLongInts, long)

IMPLEMENT_QRBG_GETTER(getInt8, int8)
IMPLEMENT_QRBG_GETTER(getInt16, int16)
IMPLEMENT_QRBG_GETTER(getInt32, int32)
IMPLEMENT_QRBG_GETTER(getInt64, int64)

IMPLEMENT_QRBG_GETTER(getUInt8, uint8)
IMPLEMENT_QRBG_GETTER(getUInt16, uint16)
IMPLEMENT_QRBG_GETTER(getUInt32, uint32)
IMPLEMENT_QRBG_GETTER(getUInt64, uint64)

IMPLEMENT_QRBG_ARRAY_GETTER(getInt8s, int8)
IMPLEMENT_QRBG_ARRAY_GETTER(getInt16s, int16)
IMPLEMENT_QRBG_ARRAY_GETTER(getInt32s, int32)
IMPLEMENT_QRBG_ARRAY_GETTER(getInt64s, int64)

IMPLEMENT_QRBG_ARRAY_GETTER(getUInt8s, uint8)
IMPLEMENT_QRBG_ARRAY_GETTER(getUInt16s, uint16)
IMPLEMENT_QRBG_ARRAY_GETTER(getUInt32s, uint32)
IMPLEMENT_QRBG_ARRAY_GETTER(getUInt64s, uint64)

//
// floating point getter methods
// (these also work on all types of byte ordered machines)
//

// returns: normalized float in range [0, 1>
float QRBG::getFloat() throw(ConnectError, CommunicationError, ServiceDenied) {
#if 0
	//uint32 data = 0x3F800000uL | (getInt32() & 0x00FFFFFFuL);
	//return *((float*)&data) - 1.0f;
        return getUInt32() * (1.0f/4294967296.0f);
        //divided by 2^32 (biggest uint32 is 2^32-1)
#else
        //We need 24 bits.
        uint8 data[3];
#if 0
        size_t acquired;
        
        acquired = getUInt8s(data, 3);
        ASSERT(acquired == 3 );
#else
        getUInt8s(data, 3);
#endif
        uint32 a = ( ((uint32) data[0]) << 16 ) | ( ((uint32) data[1]) << 8 ) | ( ((uint32) data[2]) );
        return a * (1.0f / 16777216.0f);
#endif
}

// returns: normalized double in range [0, 1> 
double QRBG::getDouble() throw(ConnectError, CommunicationError, ServiceDenied) {
#if 0
//	uint64 data = 0x3FF0000000000000uLL | (getInt64() & 0x000FFFFFFFFFFFFFuLL);
//	return *((double*)&data) - 1.0;

//  Assuming 53bits resolution
//  uint32 a=getUInt32()>>5, b=getUInt32()>>6; 
//  return(a*67108864.0+b)*(1.0/9007199254740992.0);

  return getUInt64() * (1.0 / 18446744073709551616.0 );
  //divided by 2^64
#else
        //We need just 53 bits. We will get 56 bits and discard last 3 bits
        uint8 data[7];
#if 0
        size_t acquired;
        
        acquired = getUInt8s(data, 7);
        ASSERT(acquired == 7 );
#else
        getUInt8s(data, 7);
#endif
        uint64 a = ( ((uint64) data[0]) << 45 ) | 
                   ( ((uint64) data[1]) << 37 ) | 
                   ( ((uint64) data[2]) << 29 ) |
                   ( ((uint64) data[3]) << 21 ) |
                   ( ((uint64) data[4]) << 13 ) |
                   ( ((uint64) data[5]) << 5  ) |
                   ( ((uint64) data[6]) >> 3  );
        return a * (1.0 / 9007199254740992.0);
#endif
}

// returns: array of normalized floats in range [0, 1>
size_t QRBG::getFloats(float* buffer, size_t count) throw(ConnectError, CommunicationError, ServiceDenied) {

#if 0
	ASSERT(sizeof(float) == sizeof(uint32));
	size_t acquired = AcquireBytes((byte*)buffer, sizeof(uint32)*count) / sizeof(uint32);

	//register uint32 data;
	register int idx = (int)acquired;
	while (--idx >= 0) {
		//data = 0x3F800000uL | (*((uint32*)(buffer+idx)) & 0x00FFFFFFuL);
		//buffer[idx] = *((float*)&data) - 1.0f;
                buffer[idx] = *((uint32*)(buffer+idx)) * (1.0f/4294967296.0f);
	}

	return acquired;
#else
        //Assuming 24-bits mantissa
        uint8* data = new uint8[3*count];
        uint32 a;

	size_t acquired = AcquireBytes((byte*)data, 3*count) / 3;
        size_t idx;
        for (idx = 0; idx < acquired; ++idx) {
          a = ( ((uint32) data[idx*3]) << 16 ) | ( ((uint32) data[idx*3 + 1]) << 8 ) | ( ((uint32) data[idx*3 + 2]) );
          buffer[idx] =  a * (1.0f / 16777216.0f);
        }
        delete[] data;
        return acquired;
      
#endif
}

// returns: array of normalized doubles in range [0, 1>
size_t QRBG::getDoubles(double* buffer, size_t count) throw(ConnectError, CommunicationError, ServiceDenied) {
#if 0  
	ASSERT(sizeof(double) == sizeof(uint64));

	size_t acquired = AcquireBytes((byte*)buffer, sizeof(uint64)*count) / sizeof(uint64);

	register uint64 data;
	register int idx = (int)acquired;
	while (--idx >= 0) {
		//data = 0x3FF0000000000000uLL | (*((uint64*)(buffer+idx)) & 0x000FFFFFFFFFFFFFuLL);
		//buffer[idx] = *((double*)&data) - 1.0;
                buffer[idx] = *((uint64*)(buffer+idx))  * (1.0 / 18446744073709551616.0 );
	}

	return acquired;
#else
        //Assuming 53-bits mantissa
        uint8* data = new uint8[7*count];
        uint64 a;

	size_t acquired = AcquireBytes((byte*)data, 7*count) / 7;
        size_t idx;
        for (idx = 0; idx < acquired; ++idx) {
          a = ( ((uint64) data[idx*7    ]) << 45 ) |
              ( ((uint64) data[idx*7 + 1]) << 37 ) |
              ( ((uint64) data[idx*7 + 2]) << 29 ) |
              ( ((uint64) data[idx*7 + 3]) << 21 ) |
              ( ((uint64) data[idx*7 + 4]) << 13 ) |
              ( ((uint64) data[idx*7 + 5]) << 5  ) |
              ( ((uint64) data[idx*7 + 6]) >> 3  ) ;

          buffer[idx] =  a * (1.0 / 9007199254740992.0);
        }
        delete[] data;
        return acquired;

#endif        
}
