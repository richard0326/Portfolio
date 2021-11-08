#pragma once

#include "mysql/include/mysql.h"
#include "mysql/include/errmsg.h"
#pragma comment(lib, "libmysql.lib")

/////////////////////////////////////////////////////////
// MySQL DB 연결 클래스
//
// 단순하게 MySQL Connector 를 통한 DB 연결만 관리한다.
//
// 스레드에 안전하지 않으므로 주의 해야 함.
// 여러 스레드에서 동시에 이를 사용한다면 개판이 됨.
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
	// MySQL DB 연결
	//////////////////////////////////////////////////////////////////////
	bool		Connect(void);

	//////////////////////////////////////////////////////////////////////
	// MySQL DB 끊기
	//////////////////////////////////////////////////////////////////////
	bool		Disconnect(void);


	//////////////////////////////////////////////////////////////////////
	// 쿼리 날리고 결과셋 임시 보관
	//
	//////////////////////////////////////////////////////////////////////
	bool		Query(const wchar_t* szStringFormat, ...);
	bool		Query_Save(const wchar_t* szStringFormat, ...);	// DBWriter 스레드의 Save 쿼리 전용
															// 결과셋을 저장하지 않음.

	//////////////////////////////////////////////////////////////////////
	// 쿼리를 날린 뒤에 결과 뽑아오기.
	//
	// 결과가 없다면 NULL 리턴.
	//////////////////////////////////////////////////////////////////////
	MYSQL_ROW	FetchRow(void);

	//////////////////////////////////////////////////////////////////////
	// 한 쿼리에 대한 결과 모두 사용 후 정리.
	//////////////////////////////////////////////////////////////////////
	void		FreeResult(void);


	//////////////////////////////////////////////////////////////////////
	// Error 얻기.한 쿼리에 대한 결과 모두 사용 후 정리.
	//////////////////////////////////////////////////////////////////////
	int			GetLastError(void) { return m_iLastError; }
	WCHAR* GetLastErrorMsg(void) { return m_szLastErrorMsg; }


private:

	//////////////////////////////////////////////////////////////////////
	// mysql 의 LastError 를 맴버변수로 저장한다.
	//////////////////////////////////////////////////////////////////////
	void		SaveLastError(void);

private:

	//-------------------------------------------------------------
	// MySQL 연결객체 본체
	//-------------------------------------------------------------
	MYSQL		m_MySQL;

	//-------------------------------------------------------------
	// MySQL 연결객체 포인터. 위 변수의 포인터임. 
	// 이 포인터의 null 여부로 연결상태 확인.
	//-------------------------------------------------------------
	MYSQL* m_pMySQL = nullptr;

	//-------------------------------------------------------------
	// 쿼리를 날린 뒤 Result 저장소.
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