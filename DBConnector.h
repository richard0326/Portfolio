#pragma once

#include "mysql/include/mysql.h"
#include "mysql/include/errmsg.h"
#pragma comment(lib, "libmysql.lib")

/////////////////////////////////////////////////////////
// MySQL DB ���� Ŭ����
//
// �ܼ��ϰ� MySQL Connector �� ���� DB ���Ḹ �����Ѵ�.
//
// �����忡 �������� �����Ƿ� ���� �ؾ� ��.
// ���� �����忡�� ���ÿ� �̸� ����Ѵٸ� ������ ��.
//
/////////////////////////////////////////////////////////

class CDBConnector
{
public:

	enum en_DB_CONNECTOR
	{
		en_QUERY_MAX_LEN = 2048
	};

	CDBConnector(const wchar_t* szDBIP, const wchar_t* szUser, const wchar_t* szPassword, const wchar_t* szDBName, int iDBPort);
	virtual		~CDBConnector();

	//////////////////////////////////////////////////////////////////////
	// MySQL DB ����
	//////////////////////////////////////////////////////////////////////
	bool		Connect(void);

	//////////////////////////////////////////////////////////////////////
	// MySQL DB ����
	//////////////////////////////////////////////////////////////////////
	bool		Disconnect(void);


	//////////////////////////////////////////////////////////////////////
	// ���� ������ ����� �ӽ� ����
	//
	//////////////////////////////////////////////////////////////////////
	bool		Query(const wchar_t* szStringFormat, ...);
	bool		Query_Save(const wchar_t* szStringFormat, ...);	// DBWriter �������� Save ���� ����
															// ������� �������� ����.

	//////////////////////////////////////////////////////////////////////
	// ������ ���� �ڿ� ��� �̾ƿ���.
	//
	// ����� ���ٸ� NULL ����.
	//////////////////////////////////////////////////////////////////////
	MYSQL_ROW	FetchRow(void);

	//////////////////////////////////////////////////////////////////////
	// �� ������ ���� ��� ��� ��� �� ����.
	//////////////////////////////////////////////////////////////////////
	void		FreeResult(void);


	//////////////////////////////////////////////////////////////////////
	// Error ���.�� ������ ���� ��� ��� ��� �� ����.
	//////////////////////////////////////////////////////////////////////
	int			GetLastError(void) { return m_iLastError; }
	WCHAR* GetLastErrorMsg(void) { return m_szLastErrorMsg; }


private:

	//////////////////////////////////////////////////////////////////////
	// mysql �� LastError �� �ɹ������� �����Ѵ�.
	//////////////////////////////////////////////////////////////////////
	void		SaveLastError(void);

private:

	//-------------------------------------------------------------
	// MySQL ���ᰴü ��ü
	//-------------------------------------------------------------
	MYSQL		m_MySQL;

	//-------------------------------------------------------------
	// MySQL ���ᰴü ������. �� ������ ��������. 
	// �� �������� null ���η� ������� Ȯ��.
	//-------------------------------------------------------------
	MYSQL* m_pMySQL = nullptr;

	//-------------------------------------------------------------
	// ������ ���� �� Result �����.
	//
	//-------------------------------------------------------------
	MYSQL_RES* m_pSqlResult;

	WCHAR		m_szDBIP[16];
	WCHAR		m_szDBUser[64];
	WCHAR		m_szDBPassword[64];
	WCHAR		m_szDBName[64];
	int			m_iDBPort;


	WCHAR		m_szQuery[en_QUERY_MAX_LEN];
	char		m_szQueryUTF8[en_QUERY_MAX_LEN];

	int			m_iLastError;
	WCHAR		m_szLastErrorMsg[128];

	__int64		m_freq;
};

class CTlsDBConnector
{
public:
	CTlsDBConnector(const wchar_t* szDBIP, const wchar_t* szUser, const wchar_t* szPassword, const wchar_t* szDBName, int iDBPort);
	~CTlsDBConnector();

	CDBConnector* GetDBConnector();

private:
	DWORD m_tlsIndex;
	CList<CDBConnector*> m_saveForEraseList;

	WCHAR		m_szDBIP[16];
	WCHAR		m_szDBUser[64];
	WCHAR		m_szDBPassword[64];
	WCHAR		m_szDBName[64];
	int			m_iDBPort;
};