#include "stdafx.h"
#include "RedisConnector.h"
#include "Util.h"
#include <string>
using namespace NabzackoLibrary;

CRedisConnector::CRedisConnector(const wchar_t* pIPWstr, size_t port)
{
	char ipStr[16];
	ConvertWideCharToMultiByte_NoAlloc(pIPWstr, ipStr, 16);

	m_client.connect(ipStr, port);
}

CRedisConnector::~CRedisConnector()
{
	m_client.disconnect();
}

void CRedisConnector::set(long long key, const wchar_t* pValue)
{
	char keyBuf[256];
	sprintf_s(keyBuf, "%lu", key);

	char ValBuf[2048];
	ConvertWideCharToMultiByte_NoAlloc(pValue, ValBuf, 2048);

	m_client.set(keyBuf, ValBuf);
	m_client.sync_commit();
}

void CRedisConnector::set(long long key, const char* pValue)
{
	char keyBuf[256];
	sprintf_s(keyBuf, "%lu", key);

	m_client.set(keyBuf, pValue);
	m_client.sync_commit();
}

bool CRedisConnector::setIfNot(long long key, const wchar_t* pValue)
{
	char keyBuf[256];
	sprintf_s(keyBuf, "%lu", key);

	auto ret = m_client.get(keyBuf);
	m_client.sync_commit();

	auto _reply = ret.get();
	if (_reply.is_null() == false)
	{
		// 값이 비어있음.
		return false;
	}

	char ValBuf[2048];
	ConvertWideCharToMultiByte_NoAlloc(pValue, ValBuf, 2048);

	m_client.set(keyBuf, ValBuf);
	m_client.sync_commit();

	return true;
}

bool CRedisConnector::setIfNot(long long key, const char* pValue)
{
	char keyBuf[256];
	sprintf_s(keyBuf, "%lu", key);

	// 있는지 확인하는 곳
	auto ret = m_client.get(keyBuf);
	m_client.sync_commit();

	auto _reply = ret.get();
	if (_reply.is_null() == false)
	{
		// 값이 비어있음.
		return false;
	}

	m_client.set(keyBuf, pValue);
	m_client.sync_commit();

	return true;
}

bool CRedisConnector::get(long long key, char* pOutValue)
{
	char keyBuf[256];
	sprintf_s(keyBuf, "%lu", key);

	auto ret = m_client.get(keyBuf);
	m_client.sync_commit();

	auto _reply = ret.get();
	if (_reply.is_null() == true)
	{
		// 값이 비어있음.
		return false;
	}
	char* pData = (char*)_reply.as_string().data();

	int i = 0;
	for (; pData[i] != '\0'; ++i)
	{
		pOutValue[i] = pData[i];
	}
	pOutValue[i] = '\0';

	return true;
}

bool CRedisConnector::get(long long key, wchar_t* pOutValue)
{
	char keyBuf[256];
	sprintf_s(keyBuf, "%lu", key);
	auto ret = m_client.get(keyBuf);
	m_client.sync_commit();

	auto _reply = ret.get();
	if (_reply.is_null() == true)
	{
		// 값이 비어있음.
		return false;
	}

	wchar_t ValueBuf[2048];
	ConvertMultiByteToWideChar_NoAlloc(_reply.as_string().data(), ValueBuf, 2048);

	int i = 0;
	for (; ValueBuf[i] != '\0'; ++i)
	{
		pOutValue[i] = ValueBuf[i];
	}
	pOutValue[i] = '\0';

	return true;
}

bool CRedisConnector::getDel(long long key, char* pOutValue)
{
	char keyBuf[256];
	sprintf_s(keyBuf, "%lu", key);

	auto ret = m_client.get(keyBuf);
	m_client.sync_commit();

	auto _reply = ret.get();
	if (_reply.is_null() == true)
	{
		// 값이 비어있음.
		return false;
	}

	char* pData = (char*)_reply.as_string().data();
	int i = 0;
	for (; pData[i] != '\0'; ++i)
	{
		pOutValue[i] = pData[i];
	}
	pOutValue[i] = '\0';

	std::vector<std::string> input;
	input.push_back(keyBuf);
	m_client.del(input);
	m_client.sync_commit();

	return true;
}

bool CRedisConnector::getDel(long long key, wchar_t* pOutValue)
{
	char keyBuf[256];
	sprintf_s(keyBuf, "%lu", key);
	auto ret = m_client.get(keyBuf);
	m_client.sync_commit();

	auto _reply = ret.get();
	if (_reply.is_null() == true)
	{
		// 값이 비어있음.
		return false;
	}

	wchar_t ValueBuf[2048];
	ConvertMultiByteToWideChar_NoAlloc(_reply.as_string().data(), ValueBuf, 2048);

	int i = 0;
	for (; ValueBuf[i] != '\0'; ++i)
	{
		pOutValue[i] = ValueBuf[i];
	}
	pOutValue[i] = '\0';

	std::vector<std::string> input;
	input.push_back(keyBuf);
	m_client.del(input);
	m_client.sync_commit();

	return true;
}