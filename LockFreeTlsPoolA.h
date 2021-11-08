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
	// 생성자
	// poolSize : chunk 개수
	// chunkSize : chunk 내부 노드 개수
	// placementNew : 할당시 생성자/소멸자 호출 여부
	// placementOnce : 메모리풀 처음 생성될때만 생성자/소멸자 호출 여부
	// aligned : 메모리 줄 맞춰서 생성할지 여부
	// pageLock : 페이지락 한 상태로 생성할지 여부
	CLockFreeTlsPoolA(int poolSize, int chunkSize, bool placementNew = false, bool placementOnce = false, int aligned = 0, bool pageLock = false)
	{
		// 인자 복사
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

		// 소멸자 호출용 Chunk 포인터 배열
		m_pDestructorArr = new st_Chunk * [m_poolSize];

		// chunkPool 생성
		m_chunkPool = new CLockFreePool<st_Chunk>(m_poolSize, false, false, aligned, pageLock);
		for (int j = 0; j < m_poolSize; ++j)
		{
			// chunkPool의 chunk 할당 받아서 내부에 Node 배열을 생성해주기
			// 그리고 반납하기
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
		// tls에 저장된 chunk를 가져오기
		st_Chunk* pChunk = (st_Chunk*)TlsGetValue(m_tlsIndex);
		
		// 없는 경우 chunk를 할당 받기
		if (pChunk == nullptr)
		{
			pChunk = m_chunkPool->Alloc();
			if (pChunk == nullptr)
				return nullptr;

			// chunk 초기화
			InterlockedIncrement(&m_allocCount);
			pChunk->m_allocCount = m_chunkSize;
			pChunk->m_freeCount = m_chunkSize;
			TlsSetValue(m_tlsIndex, pChunk);
		}
#ifdef MY_DEBUG
		InterlockedIncrement(&m_NodeCount);
#endif

		// chunk 내부의 Node를 pRet 변수로 옮긴다.
		--pChunk->m_allocCount;
		T* pRet = (T*)&pChunk->m_pChunk[pChunk->m_allocCount];

		// 내부 Node를 다 사용한 경우 새로운 chunk를 할당한다.
		// 반납은 free에서 하도록 설계함
		if (0 == pChunk->m_allocCount)
		{
			st_Chunk* pNextChunk = m_chunkPool->Alloc();

			if (pNextChunk != nullptr)
			{
				// 새로운 chunk에 할당 성공한 경우
				pNextChunk->m_allocCount = m_chunkSize;
				pNextChunk->m_freeCount = m_chunkSize;
				TlsSetValue(m_tlsIndex, pNextChunk);

				InterlockedIncrement(&m_allocCount);
			}
			else
			{
				// 새로운 chunk에 할당 실패한 경우
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
		if (0 == InterlockedDecrement(pChunkNode->pFreeCount))	// xor eax eax 유도 조건문
		{
			// 모든 Node를 다 반납한 경우...
			st_Chunk* pChunk = pChunkNode->pThisChunk;
			m_chunkPool->Free(pChunk);	// chunkPool에 chunk를 반납한다.
			InterlockedIncrement(&m_freeCount);
		}

#ifdef MY_DEBUG
		InterlockedDecrement(&m_NodeCount);
#endif
	}

	// chunk의 할당 개수
	__forceinline int GetSize()
	{
		return m_allocCount - m_freeCount;
	}

	// chunk의 할당 개수
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

	// chunk의 전체 개수
	int GetMaxChunkSize()
	{
		return m_poolSize;
	}

	// node의 최대 개수
	int GetMaxNodeSize()
	{
		return m_chunkSize;
	}

private:
	// readWrite Memory
	__declspec(align(64)) long m_allocCount = 0;	// 할당 Chunk 개수
	__declspec(align(64)) long m_freeCount = 0;		// 해제 Chunk 개수
#ifdef MY_DEBUG
	__declspec(align(64)) long m_NodeCount = 0;		// 할당된 노드 개수
#endif
	// readOnly Memory
	int m_chunkSize = 0;					// Chunk 개수
	DWORD m_tlsIndex;						// tls 인덱스
	CLockFreePool<st_Chunk>* m_chunkPool;	// 메모리풀
	st_Chunk** m_pDestructorArr;			// 소멸할때 사용할
	bool m_bPlacementNew;					// 할당 마다 생성자를 호출
	bool m_bPlacementOnce;					// 처음에만 생성자를 호출
	int m_poolSize = 0;						// chunk의 크기
};