#pragma once
#include <cpp_redis/cpp_redis>
#pragma comment (lib, "cpp_redis.lib")
#pragma comment (lib, "tacopie.lib")
#pragma comment (lib, "ws2_32.lib")

// ���������� winsock dll�� �ʱ�ȭ ���������.
//WORD version = MAKEWORD(2, 2);
//WSADATA data;
//WSAStartup(version, &data);

class CRedisConnector
{
public:
	CRedisConnector(const wchar_t* pIPWstr, size_t port);
	~CRedisConnector();

	void set(long long key, const wchar_t* pValue);
	void set(long long key, const char* pValue);
	bool setIfNot(long long key, const wchar_t* pValue);
	bool setIfNot(long long key, const char* pValue);
	bool get(long long key, char* pOutValue);
	bool get(long long key, wchar_t* pOutValue);
	bool getDel(long long key, char* pOutValue);
	bool getDel(long long key, wchar_t* pOutValue);

private:
	cpp_redis::client m_client;
};