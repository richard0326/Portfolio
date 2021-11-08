#pragma once

#include "LockFreePool.h""

template <typename T>
class CLockFreeTlsPoolA
{
	struct st_Chunk;
	struct st_ChunkNode
	{
		T data;
		st_Chunk* pThisChunk = nullptr;
		long* pFreeCount = nullptr;
	};	

	struct st_Chunk
	{
		// readWrite Memory
		long m_freeCount = 0;
		int m_allocCount = 0;
		// readOnly Memory
		alignas(64) st_ChunkNode* m_pChunk = nullptr;
	};

public:
	// ������
	// poolSize : chunk ����
	// chunkSize : chunk ���� ��� ����
	// placementNew : �Ҵ�� ������/�Ҹ��� ȣ�� ����
	// placementOnce : �޸�Ǯ ó�� �����ɶ��� ������/�Ҹ��� ȣ�� ����
	// aligned : �޸� �� ���缭 �������� ����
	// pageLock : �������� �� ���·� �������� ����
	CLockFreeTlsPoolA(int poolSize, int chunkSize, bool placementNew = false, bool placementOnce = false, int aligned = 0, bool pageLock = false)
	{
		// ���� ����
		m_bPlacementNew = placementNew;
		m_bPlacementOnce = placementOnce;
		m_poolSize = poolSize;
		m_chunkSize = chunkSize;
		m_tlsIndex = TlsAlloc();
		if (m_tlsIndex == TLS_OUT_OF_INDEXES)
		{
			int* crash = nullptr;
			*crash = 0;
		}

		// �Ҹ��� ȣ��� Chunk ������ �迭
		m_pDestructorArr = new st_Chunk * [m_poolSize];

		// chunkPool ����
		m_chunkPool = new CLockFreePool<st_Chunk>(m_poolSize, false, false, aligned, pageLock);
		for (int j = 0; j < m_poolSize; ++j)
		{
			// chunkPool�� chunk �Ҵ� �޾Ƽ� ���ο� Node �迭�� �������ֱ�
			// �׸��� �ݳ��ϱ�
			st_Chunk* pChunk = m_chunkPool->Alloc(); 

			if (pChunk->m_pChunk == nullptr)
			{
				pChunk->m_pChunk = (st_ChunkNode*)malloc(m_chunkSize * sizeof(st_ChunkNode));
				if (pChunk->m_pChunk == nullptr)
				{
					int* crash = nullptr;
					*crash = 0;
				}

				for (int i = 0; i < m_chunkSize; ++i)
				{
					if (m_bPlacementOnce == true)
						new (&pChunk->m_pChunk[i].data) T;
					pChunk->m_pChunk[i].pThisChunk = pChunk;

					pChunk->m_pChunk[i].pFreeCount = &pChunk->m_freeCount;
				}
			}
			else
			{
				int* crash = nullptr;
				*crash = 0;
			}

			pChunk->m_freeCount = 0;
			m_pDestructorArr[j] = pChunk;
		}

		for (int i = 0; i < m_poolSize; ++i)
		{
			m_chunkPool->Free(m_pDestructorArr[i]);
		}
	}

	~CLockFreeTlsPoolA()
	{
		for (int i = 0; i < m_poolSize; ++i)
		{
			if (m_bPlacementOnce == true)
			{
				for (int j = 0; j < m_chunkSize; ++j)
				{
					m_pDestructorArr[i]->m_pChunk[i].data.~T();
				}
			}
			free(m_pDestructorArr[i]->m_pChunk);
		}

		delete m_pDestructorArr;
		delete m_chunkPool;
	}

	__forceinline T* Alloc()
	{
		// tls�� ����� chunk�� ��������
		st_Chunk* pChunk = (st_Chunk*)TlsGetValue(m_tlsIndex);
		
		// ���� ��� chunk�� �Ҵ� �ޱ�
		if (pChunk == nullptr)
		{
			pChunk = m_chunkPool->Alloc();
			if (pChunk == nullptr)
				return nullptr;

			// chunk �ʱ�ȭ
			InterlockedIncrement(&m_allocCount);
			pChunk->m_allocCount = m_chunkSize;
			pChunk->m_freeCount = m_chunkSize;
			TlsSetValue(m_tlsIndex, pChunk);
		}
#ifdef MY_DEBUG
		InterlockedIncrement(&m_NodeCount);
#endif

		// chunk ������ Node�� pRet ������ �ű��.
		--pChunk->m_allocCount;
		T* pRet = (T*)&pChunk->m_pChunk[pChunk->m_allocCount];

		// ���� Node�� �� ����� ��� ���ο� chunk�� �Ҵ��Ѵ�.
		// �ݳ��� free���� �ϵ��� ������
		if (0 == pChunk->m_allocCount)
		{
			st_Chunk* pNextChunk = m_chunkPool->Alloc();

			if (pNextChunk != nullptr)
			{
				// ���ο� chunk�� �Ҵ� ������ ���
				pNextChunk->m_allocCount = m_chunkSize;
				pNextChunk->m_freeCount = m_chunkSize;
				TlsSetValue(m_tlsIndex, pNextChunk);

				InterlockedIncrement(&m_allocCount);
			}
			else
			{
				// ���ο� chunk�� �Ҵ� ������ ���
				TlsSetValue(m_tlsIndex, nullptr);
				return nullptr;
			}
		}

		if (m_bPlacementNew)
			new (pRet) T;

		return pRet;
	}

	__forceinline void Free(T* pReturn)
	{
		if (pReturn == nullptr)
			return ;

		if (m_bPlacementNew)
			pReturn->~T();

		st_ChunkNode* pChunkNode = (st_ChunkNode*)pReturn;
		if (0 == InterlockedDecrement(pChunkNode->pFreeCount))	// xor eax eax ���� ���ǹ�
		{
			// ��� Node�� �� �ݳ��� ���...
			st_Chunk* pChunk = pChunkNode->pThisChunk;
			m_chunkPool->Free(pChunk);	// chunkPool�� chunk�� �ݳ��Ѵ�.
			InterlockedIncrement(&m_freeCount);
		}

#ifdef MY_DEBUG
		InterlockedDecrement(&m_NodeCount);
#endif
	}

	// chunk�� �Ҵ� ����
	__forceinline int GetSize()
	{
		return m_allocCount - m_freeCount;
	}

	// chunk�� �Ҵ� ����
	__forceinline inline int GetChunkSize()
	{
		return m_allocCount - m_freeCount;
	}

#ifdef MY_DEBUG
	int GetNodeSize()
	{
		return m_NodeCount;
	}
#endif

	// chunk�� ��ü ����
	int GetMaxChunkSize()
	{
		return m_poolSize;
	}

	// node�� �ִ� ����
	int GetMaxNodeSize()
	{
		return m_chunkSize;
	}

private:
	// readWrite Memory
	__declspec(align(64)) long m_allocCount = 0;	// �Ҵ� Chunk ����
	__declspec(align(64)) long m_freeCount = 0;		// ���� Chunk ����
#ifdef MY_DEBUG
	__declspec(align(64)) long m_NodeCount = 0;		// �Ҵ�� ��� ����
#endif
	// readOnly Memory
	int m_chunkSize = 0;					// Chunk ����
	DWORD m_tlsIndex;						// tls �ε���
	CLockFreePool<st_Chunk>* m_chunkPool;	// �޸�Ǯ
	st_Chunk** m_pDestructorArr;			// �Ҹ��Ҷ� �����
	bool m_bPlacementNew;					// �Ҵ� ���� �����ڸ� ȣ��
	bool m_bPlacementOnce;					// ó������ �����ڸ� ȣ��
	int m_poolSize = 0;						// chunk�� ũ��
};