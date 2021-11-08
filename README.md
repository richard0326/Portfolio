- 2021.11.03  

면접 후 포트폴리오에 대한 부족함을 느끼고 내용을 추가하기 위해서 만든 저장소입니다.  
빠른 시일 내에 더 추가하도록 하겠습니다.  
소스 코드는 메일로 요청하시면 일부 파일을 추가 공개할 수 있음.  
메일 : richard0326@naver.com  

- 2021.11.06     
  
포트폴리오에 대한 설명을 더 요청함.   
컨텐츠 제작 경험 부족을 지적 받음.   
시험에서 틀렸던 부분들은 대체로 그래픽스, DB에 대한 부분들이였음.   

- 2021.11.08  
   
기존 pdf에서 조금 부족한 내용들을 글로 추가   
[pdf에 있던 안정화 테스트 영상 링크](https://www.youtube.com/watch?v=Y7Du3PCgPkg)  
  
서버 테스트 환경 경험에서 테스트 환경  
[안정화 테스트 환경 이미지](https://github.com/richard0326/Portfolio/blob/main/2.PNG)  
  
[NetServer Header file](https://github.com/richard0326/Portfolio/blob/main/NetServer.h)  

[MMORPGServer Header file](https://github.com/richard0326/Portfolio/blob/main/MMOServer.h)   
  
- 설명  
메모리 로그 관련 : DEBUG_LOCATION, st_ForDebug   
일부 모니터링 정보는 디버그 모드에서만 출력되도록 진행함.  
   
중요한 정보는 en_SESSION_STATE 부분에서 AUTH 부분과 GAME 부분으로 나눠지는데  
이것은 DB에서 데이터를 로드하기 전에는 AUTH 부분에서 싱글 스레드 방식으로 처리하고,  
이후에 플레이어에 대한 데이터를 모두 로드했다면, GAME 스레드 부분으로 넘겨주어서  
DB에 의해서 성능적으로 느려지는 부분을 막습니다.  
   
헤더 파일에 이것 저것 많이 들어간 이유는  
초기화 할때, st_SESSION을 상속 받아서 컨텐츠에서 st_PLAYER를 동적할당해서 MMORPGServer로 넘겨주도록 만들었기 때문이다.  
MMORPGServer 내부 스레드에서 폴링하면서 st_Session을 직접 접근하기 때문에 그렇게 됨.  
  
가상함수를 스레드 별로 두었는데, 이는 외부 컨텐츠를 짤때, 스레드를 고려한 코딩이 필요하기 때문에 그렇다.  
예) OnAuth_XXXX, OnGame_XXXX  
  





채팅 라이브러리 헤더 파일, 시퀀스다이어그림
   
