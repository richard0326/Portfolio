#include "stdafx.h"
#include <Windows.h>
#include <iostream>
#include <strsafe.h>
#include "List.h"
#include "DBConnector.h"
#include "Util.h"
#include "SystemLog.h"
using namespace NabzackoLibrary;

// ������ INSERT ���Ƿκ��� AUTO_INCREMENT �÷��� ���� ������ ID�� ��ȯ�Ѵ�.
// insert �� ��쿡 �ѿ��� ����ϵ��� ����
//printf("Last inserted record has id %d\n", mysql_insert_id(connection));

class CMySQLInitializer
{
public:
	CMySQLInitializer();
	~CMySQLInitializer();

private:
	HINSTANCE m_hModule = NULL;
};

CMySQLInitializer::CMySQLInitializer()
{
	m_hModule = LoadLibrary(L"libmysql.dll");
	if (m_hModule == NULL) {
		printf("libmysql.dll load fail : %d\n", GetLastError());

		int* crash = nullptr;
		*crash = 0;
	}
}

CMySQLInitializer::~CMySQLInitializer()
{
	if (m_hModule != nullptr)
	{
		FreeLibrary(m_hModule);
	}
}

CMySQLInitializer g_InitDll;	// �� ��ü���� DB�� ���� �����ϴ� ��ü�� ������ �Ѵ�.

CDBConnector::CDBConnector(const wchar_t* szDBIP, const wchar_t* szUser, const wchar_t* szPassword, const wchar_t* szDBName, int iDBPort)
{
	wcscpy_s(m_szDBIP, 16, szDBIP);
	wcscpy_s(m_szDBUser, 64, szUser);
	wcscpy_s(m_szDBPassword, 64, szPassword);
	wcscpy_s(m_szDBName, 64, szDBName);
	m_iDBPort = iDBPort;

	if (Connect() == false)
	{
		int* crash = nullptr;
		*crash = 0;
	}
}

CDBConnector::~CDBConnector()
{
	if (m_pMySQL != nullptr)
	{
		// DB ����ݱ�
		mysql_close(m_pMySQL);
	}
}

//////////////////////////////////////////////////////////////////////
// MySQL DB ����
//////////////////////////////////////////////////////////////////////
bool		CDBConnector::Connect(void)
{
	if (m_pMySQL != nullptr)
	{
		LOG(L"dbConnector", en_LOG_LEVEL::LEVEL_ERROR, L"Already Connected");
		return false;
	}
	// �ʱ�ȭ
	mysql_init(&m_MySQL);

	char chDBIP[16];
	char chDBUser[64];
	char chDBPassword[64];
	char chDBName[64];

	ConvertWideCharToMultiByte_NoAlloc(m_szDBIP, chDBIP, 16);
	ConvertWideCharToMultiByte_NoAlloc(m_szDBUser, chDBUser, 64);
	ConvertWideCharToMultiByte_NoAlloc(m_szDBPassword, chDBPassword, 64);
	ConvertWideCharToMultiByte_NoAlloc(m_szDBName, chDBName, 64);

	// DB ����
	m_pMySQL = mysql_real_connect(&m_MySQL, chDBIP, chDBUser, chDBPassword, chDBName, m_iDBPort, (char*)NULL, CLIENT_MULTI_STATEMENTS);
	if (m_pMySQL == NULL)
	{
		LOG(L"dbConnector", en_LOG_LEVEL::LEVEL_ERROR, L"ConnectFail");
		return false;
	}

	//�ѱۻ���������߰�.
	mysql_set_character_set(m_pMySQL, "utf8");

	mysql_query(m_pMySQL, "set session character_set_connection=euckr;");
	mysql_query(m_pMySQL, "set session character_set_results=euckr;");
	mysql_query(m_pMySQL, "set session character_set_client=euckr;");

	// mysql_ping �Լ��� ����� ��
	// ������ ���� ��� �翬���� ���ϸ� 1�� ���� �־����.
	my_bool        bReconnect = 1;
	mysql_options(m_pMySQL, MYSQL_OPT_RECONNECT, &bReconnect);

	return true;
}

//////////////////////////////////////////////////////////////////////
// MySQL DB ����
//////////////////////////////////////////////////////////////////////
bool		CDBConnector::Disconnect(void)
{
	// DB ����ݱ�
	mysql_close(m_pMySQL);
	m_pMySQL = nullptr;
	return true;
}


//////////////////////////////////////////////////////////////////////
// ���� ������ ����� �ӽ� ����
//////////////////////////////////////////////////////////////////////
bool		CDBConnector::Query(const wchar_t* szStringFormat, ...)
{
	va_list ap;
	va_start(ap, szStringFormat);
	
	HRESULT hResult = StringCchVPrintf(m_szQuery, en_QUERY_MAX_LEN, szStringFormat, ap);
	if (hResult != S_OK)
	{
		// ũ�Ⱑ �����ؼ� �ű��� ���� ���.
		LOG(L"dbConnector", en_LOG_LEVEL::LEVEL_ERROR, L"Not Enough Buffer1");
		return false;
	}
	else
	{
		va_end(ap);
	}

	ConvertWideCharToMultiByte_NoAlloc(m_szQuery, m_szQueryUTF8, en_QUERY_MAX_LEN);
	
	bool isConnected = false;
	for (int i = 0; i < 5; ++i)
	{
		int pingRet = mysql_ping(m_pMySQL);
		if (pingRet == 0)
		{
			isConnected = true;
			break;
		}
		Sleep(5);
	}

	if (isConnected == false)
	{
		SaveLastError();
		return false;
	}

	LARGE_INTEGER beforeQuery;
	LARGE_INTEGER afterQuery;
	QueryPerformanceCounter(&beforeQuery);

	int query_stat = mysql_query(m_pMySQL, m_szQueryUTF8);
	if (query_stat != 0)
	{
		SaveLastError();
		return false;
	}

	QueryPerformanceCounter(&afterQuery);
	long long value = (afterQuery.QuadPart - beforeQuery.QuadPart);
	if (value >= 10000000)
	{
		LOG(L"dbSlowQuery", en_LOG_LEVEL::LEVEL_ERROR, L"%d / 100ns, Qeury : %s", m_szQuery);
	}

	// ������
	m_pSqlResult = mysql_store_result(m_pMySQL);		// ��� ��ü�� �̸� ������
	if(m_pSqlResult == nullptr)
	{
		SaveLastError();
		return false;
	}

	return true;
}

// DBWriter �������� Save ���� ����
// ������� �������� ����.
bool		CDBConnector::Query_Save(const wchar_t* szStringFormat, ...)
{
	va_list ap;
	va_start(ap, szStringFormat);

	HRESULT hResult = StringCchVPrintf(m_szQuery, en_QUERY_MAX_LEN, szStringFormat, ap);
	if (hResult != S_OK)
	{
		// ũ�Ⱑ �����ؼ� �ű��� ���� ���.
		LOG(L"dbConnector", en_LOG_LEVEL::LEVEL_ERROR, L"Not Enough Buffer2");
		return false;
	}
	else
	{
		va_end(ap);
	}

	ConvertWideCharToMultiByte_NoAlloc(m_szQuery, m_szQueryUTF8, en_QUERY_MAX_LEN);

	bool isConnected = false;
	for (int i = 0; i < 5; ++i)
	{
		int pingRet = mysql_ping(m_pMySQL);
		if (pingRet == 0)
		{
			isConnected = true;
			break;
		}
		Sleep(5);
	}

	if (isConnected == false)
	{
		SaveLastError();
		return false;
	}

	LARGE_INTEGER beforeQuery;
	LARGE_INTEGER afterQuery;
	QueryPerformanceCounter(&beforeQuery);

	int query_stat = mysql_query(m_pMySQL, m_szQueryUTF8);
	if (query_stat != 0)
	{
		SaveLastError();
		return false;
	}

	QueryPerformanceCounter(&afterQuery);
	long long value = (afterQuery.QuadPart - beforeQuery.QuadPart);
	if (value >= 10000000)
	{
		LOG(L"dbSlowQuery", en_LOG_LEVEL::LEVEL_ERROR, L"%d / 100ns, Qeury : %s", m_szQuery);
	}

	// ������
	m_pSqlResult = mysql_store_result(m_pMySQL);		// ��� ��ü�� �̸� ������
	if (m_pSqlResult == nullptr)
	{
		SaveLastError();
		return false;
	}

	mysql_free_result(m_pSqlResult);
	return true;
}

//////////////////////////////////////////////////////////////////////
// ������ ���� �ڿ� ��� �̾ƿ���.
//
// ����� ���ٸ� NULL ����.
//////////////////////////////////////////////////////////////////////
MYSQL_ROW	CDBConnector::FetchRow(void)
{
	if (m_pSqlResult == nullptr)
		return nullptr;

	return mysql_fetch_row(m_pSqlResult);
}

//////////////////////////////////////////////////////////////////////
// �� ������ ���� ��� ��� ��� �� ����.
//////////////////////////////////////////////////////////////////////
void		CDBConnector::FreeResult(void)
{
	if (m_pSqlResult == nullptr)
		return ;
	
	mysql_free_result(m_pSqlResult);
}

//////////////////////////////////////////////////////////////////////
// mysql �� LastError �� �ɹ������� �����Ѵ�.
//////////////////////////////////////////////////////////////////////
void		CDBConnector::SaveLastError(void)
{
	m_iLastError = mysql_errno(m_pMySQL);
	const char* pErrStr = mysql_error(m_pMySQL);
	ConvertMultiByteToWideChar_NoAlloc(pErrStr, m_szLastErrorMsg, 128);

	if (m_iLastError == CR_SOCKET_CREATE_ERROR ||
		m_iLastError == CR_CONNECTION_ERROR ||
		m_iLastError == CR_CONN_HOST_ERROR ||
		m_iLastError == CR_SERVER_GONE_ERROR ||
		m_iLastError == CR_TCP_CONNECTION ||
		m_iLastError == CR_SERVER_HANDSHAKE_ERR ||
		m_iLastError == CR_SERVER_LOST ||
		m_iLastError == CR_INVALID_CONN_HANDLE)
	{
		// Log �����...
		// �׿� ������ ���� �ܰ迡�� ��ƾ���.
	}

	LOG(L"dbConnector", en_LOG_LEVEL::LEVEL_ERROR, L"[%d] %s ", m_iLastError, m_szLastErrorMsg);
}

CTlsDBConnector::CTlsDBConnector(const wchar_t* szDBIP, const wchar_t* szUser, const wchar_t* szPassword, const wchar_t* szDBName, int iDBPort)
{
	m_tlsIndex = TlsAlloc();
	if (m_tlsIndex == TLS_OUT_OF_INDEXES)
	{
		int* crash = nullptr;
		*crash = 0;
	}

	wcscpy_s(m_szDBIP, 16, szDBIP);
	wcscpy_s(m_szDBUser, 64, szUser);
	wcscpy_s(m_szDBPassword, 64, szPassword);
	wcscpy_s(m_szDBName, 64, szDBName);
	m_iDBPort = iDBPort;
}

CTlsDBConnector::~CTlsDBConnector()
{
	auto iter = m_saveForEraseList.begin();
	auto end = m_saveForEraseList.end();

	for (; iter != end; )
	{
		CDBConnector* pErase = (*iter);
		iter = m_saveForEraseList.erase(iter);

		delete pErase;
	}
}

CDBConnector* CTlsDBConnector::GetDBConnector()
{
	CDBConnector* dbConnector = (CDBConnector*)TlsGetValue(m_tlsIndex);
	if (dbConnector == nullptr)
	{
		dbConnector = new CDBConnector(m_szDBIP, m_szDBUser, m_szDBPassword, m_szDBName, m_iDBPort);
		TlsSetValue(m_tlsIndex, dbConnector);
		m_saveForEraseList.push_back(dbConnector);
	}

	return dbConnector;
}