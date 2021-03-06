#include "StdAfx.h"
#pragma comment(lib,"wininet")
#pragma comment(lib,"Advapi32")

#define BUFFERSIZE 1048576
#define OUTBUFFERSIZE 1048576

LPCSTR CLibWebQQ::g_referer_webqq="http://webqq.qq.com/";
LPCSTR CLibWebQQ::g_referer_webproxy="http://web-proxy2.qq.com/";
LPCSTR CLibWebQQ::g_domain_qq="http://qq.com";
LPCSTR CLibWebQQ::g_referer_main="http://webqq.qq.com/main.shtml?direct__2";
LPCSTR CLibWebQQ::g_referer_web2="http://web2.qq.com/";
LPCSTR CLibWebQQ::g_referer_web2proxy="http://s.web2.qq.com/proxy.html?v=20101025002";
LPCSTR CLibWebQQ::g_server_web2_connection="s.web2.qq.com";

CLibWebQQ::CLibWebQQ(DWORD dwQQID, LPSTR pszPassword, LPVOID pvObject, WEBQQ_CALLBACK_HUB wch, HANDLE hNetlib):
	m_hEventHTML(CreateEvent(NULL,FALSE,TRUE,NULL)), 
	m_hEventCONN(CreateEvent(NULL,FALSE,TRUE,NULL)), 
	m_qqid(dwQQID), 
	m_password(strdup(pszPassword)), 
	m_userobject(pvObject), 
	m_wch(wch),
	m_status(WEBQQ_STATUS_OFFLINE),
	m_sequence(0),
	m_uv(0),
	m_buffer(NULL),
	m_outbuffer(NULL),
	m_r_cookie(0),
	m_proxyhost(NULL),
	m_appid(NULL),
	m_basepath(NULL),
	cs_0x26_timeout(0),
	cs_0x3e_next_pos(0),
	m_hInetRequest(NULL),
	m_processstatus(0),
	m_loginhide(FALSE),
	m_proxyuser(NULL),
	m_proxypass(NULL),
	m_web2_vfwebqq(NULL),
	m_web2_psessionid(NULL),
	m_useweb2(FALSE),
	m_web2_nextqunkey(0),
	m_stop(false),
	m_hNetlib(hNetlib) {
	memset(m_storage,0,10*sizeof(LPSTR));
	memset(m_web2_storage,0,10*sizeof(JSONNODE*));
	itoa(rand()%100,m_web2_clientid,10);
	ultoa(time(NULL)%1000000,m_web2_clientid+strlen(m_web2_clientid),10);
}

CLibWebQQ::~CLibWebQQ() {
	Stop();
	SetStatus(WEBQQ_STATUS_OFFLINE);

	CloseHandle(m_hEventHTML);
	CloseHandle(m_hEventCONN);

	for (map<DWORD,LPWEBQQ_OUT_PACKET>::iterator iter=m_outpackets.begin(); iter!=m_outpackets.end(); iter++) {
		LocalFree(iter->second->cmd);
		LocalFree(iter->second);
	}

	if (m_web2_vfwebqq) json_free(m_web2_vfwebqq);
	if (m_web2_psessionid) json_free(m_web2_psessionid);
	if (m_password) free(m_password);
	if (m_proxyhost) free(m_proxyhost);
	if (m_proxypass) free(m_proxypass);
	if (m_basepath) free(m_basepath);
	if (m_buffer) LocalFree(m_buffer);
	if (m_outbuffer) LocalFree(m_outbuffer);
	if (m_appid) free(m_appid);

	for (int c=0; c<10; c++) {
		if (m_storage[c]) LocalFree(m_storage[c]);
		if (m_web2_storage[c]) json_delete(m_web2_storage[c]);
	}

	Log(__FUNCTION__"(): Instance Destruction");
}

void CLibWebQQ::CleanUp() {
	// TODO: Really needed?
}

void CLibWebQQ::Start() {
	DWORD dwThread;
	CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)_ThreadProc,this,0,&dwThread);
}

void CLibWebQQ::Stop() {
	if (m_hInet) {
		HINTERNET hInet=m_hInet;
		m_stop=true;
		if (m_useweb2 && m_status>=WEBQQ_STATUS_ONLINE) web2_channel_change_status("offline");
		m_hInet=NULL;
		InternetCloseHandle(hInet);
	}
}

void CLibWebQQ::SetLoginHide(BOOL val) {
	m_loginhide=val; 
}

DWORD WINAPI CLibWebQQ::_ThreadProc(CLibWebQQ* lpParameter) {
	return lpParameter->ThreadProc();
}

void CLibWebQQ::ThreadLoop4b() {
	bool breakloop=false;

	/*
	msg_id:
    o = (new Date()).getTime(), o = (o - o % 1000) / 1000;
    o = o % 10000 * 10000;
    var n = function () {
        k++;
        return o + k;
    };
	*/

	m_sequence=time(NULL) % 10000 * 10000;

	if (!web2_channel_login()) {
		SetStatus(WEBQQ_STATUS_ERROR);
		return;
	}

loopstart:
	__try {
		while (m_status!=WEBQQ_STATUS_ERROR && m_stop==false && (breakloop=web2_channel_poll()));
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		Log("CLibWebQQ(4b) Crashed at process phase %d, trying to resume operation...",m_processstatus);
		SendToHub(WEBQQ_CALLBACK_CRASH,NULL,(LPVOID)m_processstatus);

		if (!m_storage[WEBQQ_STORAGE_PARAMS]) {
			SetStatus(WEBQQ_STATUS_ERROR);
		}

		goto loopstart;
	}

	// if (!breakloop)	LocalFree(m_outbuffer);
}

DWORD CLibWebQQ::ThreadProc() {
	if (!ProbeAppID()) return 0;
	if (m_status!=WEBQQ_STATUS_ERROR) {
		if (!PrepareParams()) return 0;
	}
	if (m_status!=WEBQQ_STATUS_ERROR) {
		if (!Negotiate()) return 0;
	}
	if (m_status!=WEBQQ_STATUS_ERROR) {
		if (!m_useweb2) {
			m_outbuffer=m_outbuffer_current=(LPSTR)LocalAlloc(LMEM_FIXED,OUTBUFFERSIZE);
			*m_outbuffer=0;
			sprintf(m_buffer,"o_cookie=%u",m_qqid);
			InternetSetCookieA(g_domain_qq,NULL,m_buffer);
		}

		RefreshCookie();

		Log(__FUNCTION__"(): Initial login complete, use engine=%s",m_useweb2?"MIMQQ4b(web2)":"MIMQQ4a(webqq)");
		if (m_useweb2)
			ThreadLoop4b();
	}

	return 0;
};

void CLibWebQQ::SetStatus(WEBQQSTATUSENUM newstatus) {
	LPARAM dwStatus=MAKELPARAM(m_status,newstatus);
	m_status=newstatus;
	SendToHub(WEBQQ_CALLBACK_CHANGESTATUS,NULL,&dwStatus);
}

void CLibWebQQ::SendToHub(DWORD dwCommand, LPSTR pszArgs, LPVOID pvCustom) {
	if (m_wch) {
		if ((dwCommand & 0xfffff000)==0xffff0000) m_processstatus=11;
		m_wch(m_userobject,dwCommand,pszArgs,pvCustom);
	}
}

void CLibWebQQ::SetProxy(LPSTR pszHost, LPSTR pszUser, LPSTR pszPass) {
	if (m_proxyhost) free(m_proxyhost);
	if (m_proxyuser) free(m_proxyuser);
	if (m_proxypass) free(m_proxypass);
	m_proxyhost=strdup(pszHost);
	if (pszUser && *pszUser) m_proxyuser=strdup(pszUser);
	if (pszPass && *pszPass) m_proxypass=strdup(pszPass);
}

LPSTR CLibWebQQ::GetArgument(LPSTR pszArgs, int n) {
	for (int c=0; c<n; c++) {
		pszArgs+=strlen(pszArgs)+1;
	}
	return pszArgs;
}

LPSTR CLibWebQQ::GetCookie(LPCSTR pszName) {
	LPSTR pszCookie=m_storage[WEBQQ_STORAGE_COOKIE];
	while (*pszCookie) {
		if (!stricmp(pszCookie,pszName)) {
			return pszCookie+strlen(pszCookie)+1;
		} else {
			pszCookie+=strlen(pszCookie);
			if (pszCookie[1]==0)
				pszCookie+=2;
			else
				pszCookie+=strlen(pszCookie+1)+3;
		}
	}

	return NULL;
}

void CLibWebQQ::Log(char *fmt,...) {
	static CHAR szLog[1024];
	va_list vl;

	va_start(vl, fmt);
	
	szLog[_vsnprintf(szLog, sizeof(szLog)-1, fmt, vl)]=0;
	SendToHub(WEBQQ_CALLBACK_DEBUGMESSAGE,szLog);

	va_end(vl);
}

DWORD CLibWebQQ::GetRND2() {
	srand(GetTickCount());
	SYSTEMTIME st;
	GetSystemTime(&st);
	return ((DWORD)floor(((double)rand()/(double)RAND_MAX)*(double)UINT_MAX+0.5f)*st.wMilliseconds)%10000000000;
}

DWORD CLibWebQQ::GetUV() {
	return m_uv?m_uv:GetRND2();
}

DWORD CLibWebQQ::GetRND() {
	srand(GetTickCount());
	return (DWORD)floor(((double)rand()/(double)RAND_MAX)*(double)100000+0.5f);
}

LPCSTR CLibWebQQ::GetNRND() {
	static char szNRND[20]={0};
	
	if (!*szNRND) {
		SYSTEMTIME st;
		GetSystemTime(&st);
		sprintf(szNRND,"F%d%d%d%d%u", st.wYear%100,st.wMonth,st.wDay,st.wMilliseconds,GetRND());
	}
	return szNRND;
}

LPCSTR CLibWebQQ::GetSSID() {
	static char szSSID[20]={0};

	if (!*szSSID) {
		sprintf(szSSID,"s%u",GetRND2());
	}
	return szSSID;
}

void CLibWebQQ::SetUseWeb2(bool val) {
	m_useweb2=val;
}

LPSTR CLibWebQQ::GetHTMLDocument(LPCSTR pszUrl, LPCSTR pszReferer, LPDWORD pdwLength, BOOL fWeb2Assign) {
	// char szFileName[MAX_PATH];
	LPSTR pszBuffer;

	*pdwLength=0;
	HINTERNET hInet=m_hInet;

	/*
	if (!(hInet=InternetOpenA("Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3 GTB5",m_proxyhost?INTERNET_OPEN_TYPE_PROXY:INTERNET_OPEN_TYPE_DIRECT,m_proxyhost,NULL,0))) {
		Log(__FUNCTION__"(): Failed initializing WinINet (InternetOpen)! Err=%d\n",GetLastError());
		*pdwLength=(DWORD)-1;
		return FALSE;
	}
	if (fWeb2Assign) this->m_hInet=hInet;

	if (m_proxyuser) {
		InternetSetOption(hInet,INTERNET_OPTION_USERNAME, m_proxyuser,  (DWORD)strlen(m_proxyuser)+1);  
		if (m_proxypass) {
			InternetSetOption(hInet,INTERNET_OPTION_PASSWORD, m_proxypass,  (DWORD)strlen(m_proxypass)+1);  
		}
	}
	*/

	LPSTR pszServer=(LPSTR)strstr(pszUrl,"//")+2;
	LPSTR pszUri=strchr(pszServer,'/');
	*pszUri=0;

	HINTERNET hInetConnect=InternetConnectA(hInet,pszServer,INTERNET_DEFAULT_HTTP_PORT,NULL,NULL,INTERNET_SERVICE_HTTP,0,NULL);
	*pszUri='/';

	if (!hInetConnect) {
		Log(__FUNCTION__"(): InternetConnectA() failed: %d, hInet=%p",GetLastError(),hInet);
		*pdwLength=(DWORD)-1;
		// InternetCloseHandle(hInet);
		return false;
	}

	HINTERNET hInetRequest=HttpOpenRequestA(hInetConnect,"GET",pszUri,NULL,pszReferer,NULL,INTERNET_FLAG_PRAGMA_NOCACHE|INTERNET_FLAG_RELOAD,NULL);
	if (!hInetRequest) {
		DWORD err=GetLastError();
		Log(__FUNCTION__"(): HttpOpenRequestA() failed: %d",err);
		InternetCloseHandle(hInetConnect);
		InternetCloseHandle(hInet);
		*pdwLength=(DWORD)-1;
		SetLastError(err);
		return false;
	}

	DWORD dwRead=70000; // poll is 60 secs timeout
	InternetSetOption(hInetRequest,INTERNET_OPTION_RECEIVE_TIMEOUT,&dwRead,sizeof(DWORD));

	if (!(HttpSendRequestA(hInetRequest,NULL,0,NULL,0))) {
		DWORD err=GetLastError();
		Log(__FUNCTION__"(): HttpSendRequestA() failed, reason=%d",err);
		InternetCloseHandle(hInetRequest);
		InternetCloseHandle(hInetConnect);
		// InternetCloseHandle(hInet);
		*pdwLength=(DWORD)-1;
		SetLastError(err);
		return false;
	}


	dwRead=0;
	DWORD dwWritten=sizeof(DWORD);
	
	HttpQueryInfo(hInetRequest/*hUrl*/,HTTP_QUERY_CONTENT_LENGTH|HTTP_QUERY_FLAG_NUMBER,pdwLength,&dwWritten,&dwRead);

	pszBuffer=NULL;

	if (strlen(pszUrl)>200 && (pszBuffer=(LPSTR)strchr(pszUrl,'?'))) *pszBuffer=0;

	if (strlen(pszUrl)<200) 
		Log(__FUNCTION__"() url=%s size=%d",pszUrl,*pdwLength);
	else
		Log(__FUNCTION__"() size=%d",*pdwLength);

	if (pszBuffer) *pszBuffer='?';

	if (!*pdwLength) *pdwLength=BUFFERSIZE;

	pszBuffer=(LPSTR)LocalAlloc(LMEM_FIXED,*pdwLength+1);
	LPSTR ppszBuffer=pszBuffer;

	while (InternetReadFile(hInetRequest/*hUrl*/,ppszBuffer,*pdwLength,&dwRead) && dwRead>0) {
		ppszBuffer+=dwRead;
		dwRead=0;
	}
	*ppszBuffer=0;
	*pdwLength=ppszBuffer-pszBuffer;

	InternetCloseHandle(hInetRequest/*hUrl*/);
	InternetCloseHandle(hInetConnect);
	// InternetCloseHandle(hInet);

	return pszBuffer;
}

LPCSTR CLibWebQQ::GetReferer(WEBQQREFERERENUM type) {
	switch (type) {
		case WEBQQ_REFERER_WEBQQ:
			return g_referer_webqq;
		case WEBQQ_REFERER_WEBPROXY:
			return g_referer_webproxy;
		case WEBQQ_REFERER_PTLOGIN:
			return m_referer_ptlogin;
		case WEBQQ_REFERER_MAIN:
			return g_referer_main;
		case WEBQQ_REFERER_WEB2:
			return g_referer_web2;
		case WEBQQ_REFERER_WEB2PROXY:
			return g_referer_web2proxy;
		default:
			return NULL;
	}
}

DWORD CLibWebQQ::AppendQuery(DWORD (*func)(CLibWebQQ*,LPSTR,LPSTR), LPSTR pszArgs) {
	// static DWORD m_sequence=0;
	DWORD ret;
	DWORD fn=func(NULL,NULL,NULL);
	DWORD ack=func(this,NULL,NULL);
	DWORD seq=0;
	LPSTR pszStart;
	bool firstpacket=(*m_outbuffer==0);
	WaitForSingleObject(m_hEventCONN,INFINITE);

	pszStart=m_outbuffer_current=m_outbuffer_current+strlen(m_outbuffer_current);

	if (fn==0x17) {
		m_outbuffer_current+=sprintf(m_outbuffer_current,"%s%u;%02x;",*m_outbuffer?"\x1d":"",m_qqid,fn);
	} else {
		m_outbuffer_current+=sprintf(m_outbuffer_current,fn>0xff?"%s%u;%04x;%u;%s;":"%s%u;%02x;%u;%s;",*m_outbuffer?"\x1d":"",m_qqid,fn,seq=m_sequence,m_storage[WEBQQ_STORAGE_PARAMS]?GetArgument(m_storage[WEBQQ_STORAGE_PARAMS],SC0X22_WEB_SESSION):"00000000");
		if (m_storage[WEBQQ_STORAGE_PARAMS]) m_sequence++;
	}
	m_outbuffer_current+=(ret=func(this,m_outbuffer_current,pszArgs));
	if (ret) {
		strcpy(m_outbuffer_current,";");
		m_outbuffer_current++;
	}
	/*
	if (ack) {
		LPWEBQQ_OUT_PACKET lpOP=m_outpackets[seq];
		if (lpOP) {
			LocalFree(lpOP->cmd);
			LocalFree(lpOP);
		}

		if (!firstpacket) pszStart++;
		lpOP=(LPWEBQQ_OUT_PACKET)LocalAlloc(LMEM_FIXED,sizeof(WEBQQ_OUT_PACKET));
		lpOP->type=fn;
		lpOP->created=GetTickCount();
		lpOP->retried=0;
		lpOP->cmd=(LPSTR)LocalAlloc(LMEM_FIXED,strlen(pszStart)+1);
		memcpy(lpOP->cmd,pszStart,strlen(pszStart)+1);
		m_outpackets[seq]=lpOP;
	}
	*/
	SetEvent(m_hEventCONN);
	return seq;
}

void CLibWebQQ::GetPasswordHash(LPCSTR pszVerifyCode, LPSTR pszOut) {
	char szTemp[50];
	char szTemp2[16];
	DWORD dwSize;

	LPSTR ppszTemp;

	HCRYPTPROV hCP;
	HCRYPTHASH hCH;
	CryptAcquireContextA(&hCP,NULL,MS_DEF_PROV_A,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT);

	// #1
	CryptCreateHash(hCP,CALG_MD5,NULL,0,&hCH);
	CryptHashData(hCH,(LPBYTE)m_password,(DWORD)strlen(m_password),0);
	dwSize=MAX_PATH;
	CryptGetHashParam(hCH,HP_HASHVAL,(LPBYTE)szTemp2,&dwSize,0);
	CryptDestroyHash(hCH);

	// #2
	CryptCreateHash(hCP,CALG_MD5,NULL,0,&hCH);
	CryptHashData(hCH,(LPBYTE)szTemp2,16,0);
	dwSize=MAX_PATH;
	CryptGetHashParam(hCH,HP_HASHVAL,(LPBYTE)szTemp,&dwSize,0);
	CryptDestroyHash(hCH);

	// #3
	CryptCreateHash(hCP,CALG_MD5,NULL,0,&hCH);
	CryptHashData(hCH,(LPBYTE)szTemp,16,0);
	dwSize=MAX_PATH;
	CryptGetHashParam(hCH,HP_HASHVAL,(LPBYTE)szTemp2,&dwSize,0);
	CryptDestroyHash(hCH);

	// #4
	CryptCreateHash(hCP,CALG_MD5,NULL,0,&hCH);
	ppszTemp=szTemp;
	for (LPSTR ppszTemp2=szTemp2; ppszTemp2-szTemp2<16; ppszTemp2++) {
		ppszTemp+=sprintf(ppszTemp,"%02X",*(LPBYTE)ppszTemp2);
	}

	strcpy(szTemp+32,pszVerifyCode);
	CryptHashData(hCH,(LPBYTE)szTemp,32+strlen(pszVerifyCode),0);
	dwSize=MAX_PATH;
	CryptGetHashParam(hCH,HP_HASHVAL,(LPBYTE)szTemp2,&dwSize,0);
	CryptDestroyHash(hCH);

	CryptReleaseContext(hCP,0);

	ppszTemp=pszOut;
	for (LPSTR ppszTemp2=szTemp2; ppszTemp2-szTemp2<16; ppszTemp2++) {
		ppszTemp+=sprintf(ppszTemp,"%02X",*(LPBYTE)ppszTemp2);
	}
}

bool CLibWebQQ::ProbeAppID() {
	SetStatus(WEBQQ_STATUS_PROBE);

	/*if (!m_useweb2)*/ {
		if (!(m_hInet=InternetOpenA("Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3 GTB5",m_proxyhost?INTERNET_OPEN_TYPE_PROXY:INTERNET_OPEN_TYPE_DIRECT,m_proxyhost,NULL,0))) {
			Log(__FUNCTION__"(): Failed initializing WinINet (InternetOpen)! Err=%d\n",GetLastError());
			SetStatus(WEBQQ_STATUS_ERROR);
			return FALSE;
		}

		if (m_proxyuser) {
			InternetSetOption(m_hInet,INTERNET_OPTION_USERNAME, m_proxyuser,  (DWORD)strlen(m_proxyuser)+1);  
			if (m_proxypass) {
				InternetSetOption(m_hInet,INTERNET_OPTION_PASSWORD, m_proxypass,  (DWORD)strlen(m_proxypass)+1);  
				free(m_proxypass);
				m_proxypass=NULL;
			}

			free(m_proxyuser);
			m_proxyuser=NULL;
		}
	}
	/*
	DWORD dwSize;
	LPSTR pszData=GetHTMLDocument("http://webqq.qq.com/",NULL,&dwSize);
	if (!pszData) {
		SetStatus(WEBQQ_STATUS_ERROR);
		return FALSE;
	}

	LPSTR ppszData=strstr(pszData,"&appid=");
	if (ppszData) {
		*strchr(ppszData+1,'&')=0;
		m_appid=strdup(ppszData+7);
		Log(__FUNCTION__"(): APPID=%s",m_appid);
	} else {
		Log(__FUNCTION__"(): Failed getting APPID!");
		LocalFree(pszData);
		SetStatus(WEBQQ_STATUS_ERROR);
		return FALSE;
	}

	LocalFree(pszData);
	*/
	m_appid=strdup("1002101");

	sprintf(m_referer_ptlogin,"http://ui.ptlogin2.qq.com/cgi-bin/login?style=4&appid=%s&enable_qlogin=0&no_verifyimg=1&s_url=http://webqq.qq.com/main.shtml?direct__2&f_url=loginerroralert",m_appid);
	return TRUE;
}

bool CLibWebQQ::PrepareParams() {
	// LPSTR pszData;
	// DWORD dwSize;
	char szTemp[30];

	SetStatus(WEBQQ_STATUS_PREPARE);
	if (!m_buffer) m_buffer=(LPSTR)LocalAlloc(LMEM_FIXED,BUFFERSIZE); // Cannot remove because RefreshCookie() needs it

/*
GET /collect?pj=1990&dm=webqq.qq.com&url=/&arg=&rdm=&rurl=&rarg=&icache=-&uv=1527858512&nu=1&ol=0&loc=http%3A//webqq.qq.com/&column=&subject=&nrnd=F106981946296&rnd=7546 HTTP/1.1
Host: trace.qq.com:80
Referer: http://webqq.qq.com/
Cookie: pgv_pvid=1527858512; pgv_flv=9.0 r100; pgv_info=ssid=s7498192482; pgv_r_cookie=106981946296
*/
	sprintf(szTemp,"pgv_pvid=%u",GetUV());
	InternetSetCookieA(g_domain_qq,NULL,szTemp);
	InternetSetCookieA(g_domain_qq,NULL,"pgv_flv=9.0 r100");

	sprintf(szTemp,"pgv_info=ssid=%s",GetSSID());
	InternetSetCookieA(g_domain_qq,NULL,szTemp);

	sprintf(szTemp,"pgv_r_cookie=%s",GetNRND()+1);
	InternetSetCookieA(g_domain_qq,NULL,szTemp);

	/*
	sprintf(m_buffer,"http://trace.qq.com:80/collect?pj=1990&dm=webqq.qq.com&url=/&arg=&rdm=&rurl=&rarg=&icache=-&uv=%u&nu=1&ol=0&loc=%s&column=&subject=&nrnd=%s&rnd=%u",GetUV(),"http%3A//webqq.qq.com/",GetNRND(),GetRND());
	if (pszData=GetHTMLDocument(m_buffer,g_referer_webqq,&dwSize)) LocalFree(pszData);
	if (dwSize==(DWORD)-1) {
		SetStatus(WEBQQ_STATUS_ERROR);
		return false;
	}
	*/

/*
GET /pingd?dm=webqq.qq.com&url=/&tt=WebQQ%20%u2013%20%u7F51%u9875%u76F4%u63A5%u804AQQ%uFF0CQQ%u65E0%u6240%u4E0D%u5728&rdm=-&rurl=-&pvid=-&scr=1024x768&scl=24-bit&lang=en&java=0&cc=undefined&pf=Linux%20i686&tz=-8&flash=9.0%20r100&ct=-&vs=3.2&column=&arg=&rarg=&ext=4&reserved1=&hurlcn=F106981946296&rand=74062 HTTP/1.1
Host: pingfore.qq.com
Referer: http://webqq.qq.com/
Cookie: pgv_pvid=1527858512; pgv_flv=9.0 r100; pgv_info=ssid=s7498192482; pgv_r_cookie=106981946296
*/
	/*
	sprintf(m_buffer,"http://pingfore.qq.com/pingd?dm=webqq.qq.com&%s&hurlcn=%s&rand=%u","url=/&tt=WebQQ%20%u2013%20%u7F51%u9875%u76F4%u63A5%u804AQQ%uFF0CQQ%u65E0%u6240%u4E0D%u5728&rdm=-&rurl=-&pvid=-&scr=1024x768&scl=24-bit&lang=en&java=0&cc=undefined&pf=Linux%20i686&tz=-8&flash=9.0%20r100&ct=-&vs=3.2&column=&arg=&rarg=&ext=4&reserved1=",GetNRND(),GetRND());
	if (pszData=GetHTMLDocument(m_buffer,g_referer_webqq,&dwSize)) LocalFree(pszData);
	if (dwSize==(DWORD)-1) {
		SetStatus(WEBQQ_STATUS_ERROR);
		return false;
	}
	*/

/*
GET /cr?id=5&d=datapt=v1.2|scripts::http%3A%2F%2Fxui.ptlogin2.%22|http%3A%2F%2Fui.ptlogin2.qq.com%2Fcgi-bin%2Flogin%3Fstyle%3D4%26appid%3D1002101%26enable_qlogin%3D0%26no_verifyimg%3D1%26s_url%3Dhttp%3A%2F%2Fwebqq.qq.com%2Fmain.shtml%3Fdirect__2%26f_url%3Dloginerroralert HTTP/1.1
Host: cr.sec.qq.com
Referer: http://ui.ptlogin2.qq.com/cgi-bin/login?style=4&appid=1002101&enable_qlogin=0&no_verifyimg=1&s_url=http://webqq.qq.com/main.shtml?direct__2&f_url=loginerroralert
Cookie: pgv_pvid=1527858512; pgv_flv=9.0 r100; pgv_info=ssid=s7498192482; pgv_r_cookie=106981946296
*/
	/*
	sprintf(m_buffer,"http://cr.sec.qq.com/cr?id=5&d=datapt=v1.2|scripts::%s%s%s","http%3A%2F%2Fxui.ptlogin2.%22|http%3A%2F%2Fui.ptlogin2.qq.com%2Fcgi-bin%2Flogin%3Fstyle%3D4%26appid%3D",m_appid,"%26enable_qlogin%3D0%26no_verifyimg%3D1%26s_url%3Dhttp%3A%2F%2Fwebqq.qq.com%2Fmain.shtml%3Fdirect__2%26f_url%3Dloginerroralert");
	if (pszData=GetHTMLDocument(m_buffer,m_referer_ptlogin,&dwSize)) LocalFree(pszData);
	if (dwSize==(DWORD)-1) {
		SetStatus(WEBQQ_STATUS_ERROR);
		return false;
	}
	*/

/*
GET /cr?id=5&d=datapp=v1.2|scripts::http%3A%2F%2F58.60.13.192%2Fcgi-bin%2Fwebqq_stat%3Fo%3D%22|scripts::http%3A%2F%2F58.60.13.192%2Fcgi-bin%2Fwebqq_stat%3Fo%3D%22|scripts::http%3A%2F%2F58.60.13.192%2Fcgi-bin%2Fwebqq%2Fwebqq_report%3Fid%3D%22|scripts::http%3A%2F%2F58.60.13.192%2Fcgi-bin%2Fwebqq%2Fwebqq_report%3Fid%3D%22|http%3A%2F%2Fwebqq.qq.com%2F HTTP/1.1
Host: cr.sec.qq.com
Referer: http://ui.ptlogin2.qq.com/cgi-bin/login?style=4&appid=1002101&enable_qlogin=0&no_verifyimg=1&s_url=http://webqq.qq.com/main.shtml?direct__2&f_url=loginerroralert
Cookie: pgv_pvid=1527858512; pgv_flv=9.0 r100; pgv_info=ssid=s7498192482; pgv_r_cookie=106981946296
*/
	/*
	if (pszData=GetHTMLDocument("http://cr.sec.qq.com/cr?id=5&d=datapp=v1.2|scripts::http%3A%2F%2F58.60.13.192%2Fcgi-bin%2Fwebqq_stat%3Fo%3D%22|scripts::http%3A%2F%2F58.60.13.192%2Fcgi-bin%2Fwebqq_stat%3Fo%3D%22|scripts::http%3A%2F%2F58.60.13.192%2Fcgi-bin%2Fwebqq%2Fwebqq_report%3Fid%3D%22|scripts::http%3A%2F%2F58.60.13.192%2Fcgi-bin%2Fwebqq%2Fwebqq_report%3Fid%3D%22|http%3A%2F%2Fwebqq.qq.com%2F",m_referer_ptlogin,&dwSize)) LocalFree(pszData);
	if (dwSize==(DWORD)-1) {
		SetStatus(WEBQQ_STATUS_ERROR);
		return false;
	}
	*/

	return true;
}

bool CLibWebQQ::Negotiate() {
	LPSTR pszData;
	DWORD dwSize;
	CHAR szCode[16]={0};
	char szTemp[MAX_PATH];

	SetStatus(WEBQQ_STATUS_NEGOTIATE);
/*
GET /check?uin=431533706&appid=1002101&r=0.1788768728924257 HTTP/1.1
Host: ptlogin2.qq.com
Referer: http://ui.ptlogin2.qq.com/cgi-bin/login?style=4&appid=1002101&enable_qlogin=0&no_verifyimg=1&s_url=http://webqq.qq.com/main.shtml?direct__2&f_url=loginerroralert
Cookie: pgv_pvid=1527858512; pgv_flv=9.0 r100; pgv_info=ssid=s7498192482; pgv_r_cookie=106981946296; ptvfsession=38b11f2837efd6e0a3cead6ab1dbaea1315345b6668ab9e9d7bf7116f139652d4a8e28c44636f8bee26bfaa1dbcf9002

ptui_checkVC('1','');
*/
	srand(GetTickCount());
	// sprintf(szTemp,"http://ptlogin2.qq.com/check?uin=%u&appid=1002101&r=%f",m_qqid,(double)rand()/(double)RAND_MAX);
	sprintf(szTemp,"http://ptlogin2.qq.com/check?uin=%u&appid=1003903&r=%f",m_qqid,(double)rand()/(double)RAND_MAX);
	if (!(pszData=GetHTMLDocument(szTemp,m_referer_ptlogin,&dwSize))) {
		SetStatus(WEBQQ_STATUS_ERROR);
		return false;;
	}

	if (strstr(pszData,"ptui_checkVC('0'")) {
		LPSTR pszCode=strstr(pszData,"','")+3;
		*strchr(pszCode,'\'')=0;
		strcpy(szCode,pszCode);
		LocalFree(pszData);
		return Login(szCode);
	} else if (strstr(pszData,"ptui_checkVC('1'")) {
		// ptui_checkVC('1','b422d0878a7017b6ed554ee6fe8d166a52b500e9e7b5e250');
		char szPath[MAX_PATH]={0};
		// DWORD dwWritten;
		LPSTR pszVC_TYPE=strstr(pszData,"','")+3;

		*strchr(pszVC_TYPE,'\'')=0;

		/*
		if (m_basepath) {
			strcat(strcpy(szPath,m_basepath),"\\");
		}
		sprintf(szPath+strlen(szPath),"verycode-%u.jpg",m_qqid);
		*/
/*
GET /getimage?aid=1002101&r=0.754857305282543 HTTP/1.1
Host: ptlogin2.qq.com
Referer: http://ui.ptlogin2.qq.com/cgi-bin/login?style=4&appid=1002101&enable_qlogin=0&no_verifyimg=1&s_url=http://webqq.qq.com/main.shtml?direct__2&f_url=loginerroralert
Cookie: pgv_pvid=1527858512; pgv_flv=9.0 r100; pgv_info=ssid=s7498192482; pgv_r_cookie=106981946296; ptvfsession=38b11f2837efd6e0a3cead6ab1dbaea1315345b6668ab9e9d7bf7116f139652d4a8e28c44636f8bee26bfaa1dbcf9002
*/
// http://captcha.qq.com/getimage?aid=1002101&r=0.8544500968419015&uin=431533706&vc_type=b422d0878a7017b6ed554ee6fe8d166a52b500e9e7b5e250

		srand(GetTickCount());
		// sprintf(m_buffer,"http://ptlogin2.qq.com/getimage?aid=%s&r=%f",m_appid,(double)rand()/(double)RAND_MAX);
		sprintf(szTemp,"http://captcha.qq.com/getimage?aid=1002101&r=%f&uin=%u&vc_type=%s",(double)rand()/(double)RAND_MAX,m_qqid,pszVC_TYPE);
		LocalFree(pszData); // Free here because pszVC_TYPE

		SendToHub(WEBQQ_CALLBACK_NEEDVERIFY,szTemp,szCode);
		if (*szCode)
			return Login(strupr(szCode));
		else {
			SetStatus(WEBQQ_STATUS_ERROR);
		}
		/*
		if (!(pszData=GetHTMLDocument(szTemp,m_referer_ptlogin,&dwSize))) {
			SetStatus(WEBQQ_STATUS_ERROR);
			return false;
		}

		HANDLE hFile=CreateFileA(szPath,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,CREATE_ALWAYS,0,NULL);
		if (hFile==INVALID_HANDLE_VALUE) {
			Log(__FUNCTION__"(): Error writing %s!",szPath);
			LocalFree(pszData);
			SetStatus(WEBQQ_STATUS_ERROR);
		} else {
			WriteFile(hFile,pszData,dwSize,&dwWritten,NULL);
			CloseHandle(hFile);
			SendToHub(WEBQQ_CALLBACK_NEEDVERIFY,szPath,szCode);
			LocalFree(pszData);
			if (*szCode)
				return Login(strupr(szCode));
			else {
				SetStatus(WEBQQ_STATUS_ERROR);
			}
		}
		*/

	}
	return false;
}

bool CLibWebQQ::Login(LPSTR pszCode) {
	char szHash[33];
	DWORD dwSize;

	SetStatus(WEBQQ_STATUS_LOGIN);

	GetPasswordHash(pszCode,szHash);

	// GET /login?u=431533706&p=346FE6613D7C77599CAA8F572AC155FF&verifycode=SDEG&remember_uin=1&aid=1002101&u1=http%3A%2F%2Fwebqq.qq.com%2Fmain.shtml%3Fdirect__2&h=1&ptredirect=1&ptlang=2052&from_ui=1&pttype=1&dumy=&fp=loginerroralert HTTP/1.1
	// GET /login?u=431533706&p=0257C25F949A61084FE69B6BC7178318&verifycode=!7LT&remember_uin=1&aid=1002101&u1=http%3A%2F%2Fweb2.qq.com%2Floginproxy.html%3Frun%3Deqq%26strong%3Dtrue&h=1&ptredirect=0&ptlang=2052&from_ui=1&pttype=1&dumy=&fp=loginerroralert	referer=ui.ptlogin2
	// GET /login?u=431533706&p=E9765C268D7E93343A681C646C37E5C5&verifycode=RMZQ&remember_uin=1&aid=1002101&u1=http%3A%2F%2Fweb2.qq.com%2Floginproxy.html%3Fstrong%3Dtrue&h=1&ptredirect=0&ptlang=2052&from_ui=1&pttype=1&dumy=&fp=loginerroralert
	sprintf(m_buffer,"http://ptlogin2.qq.com/login?u=%u&p=%s&verifycode=%s&remember_uin=1&aid=1002101&u1=%s&h=1&ptredirect=1%s&ptlang=2052&from_ui=1&pttype=1&dumy=&fp=loginerroralert",m_qqid,szHash,pszCode,"http%3A%2F%2Fweb2.qq.com%2Floginproxy.html%3Frun%3Deqq%26strong%3Dtrue",m_loginhide?"&webqq_type=1":"");
	if (LPSTR pszData=GetHTMLDocument(m_buffer,m_referer_ptlogin,&dwSize)) {
		if (!strstr(pszData,"ptuiCB('0','0','")) {
			Log("%s",pszData);
			SendToHub(WEBQQ_CALLBACK_LOGINFAIL,pszData);
			SetStatus(WEBQQ_STATUS_ERROR);
			LocalFree(pszData);
			return false;
		}
	} else {
		SetStatus(WEBQQ_STATUS_ERROR);
		return false;
	}
	return true;
}

void CLibWebQQ::RefreshCookie() {
	DWORD dwSize=BUFFERSIZE;
	int len;
	InternetGetCookieA(g_domain_qq,NULL,m_buffer,&dwSize);

	if (m_storage[WEBQQ_STORAGE_COOKIE]) LocalFree(m_storage[WEBQQ_STORAGE_COOKIE]);
	len=(int)strlen(m_buffer);
	m_storage[WEBQQ_STORAGE_COOKIE]=(LPSTR)LocalAlloc(LMEM_FIXED, len+3);
	strcpy(m_storage[WEBQQ_STORAGE_COOKIE],m_buffer);
	m_storage[WEBQQ_STORAGE_COOKIE][len]=0;
	m_storage[WEBQQ_STORAGE_COOKIE][len+1]=0;
	m_storage[WEBQQ_STORAGE_COOKIE][len+2]=0;

	LPSTR pszCookie=m_storage[WEBQQ_STORAGE_COOKIE];
	LPSTR pszCookie2=strchr(pszCookie,';');

	while (pszCookie=strchr(pszCookie,'=')) {
		if (pszCookie2!=NULL && pszCookie2<pszCookie) {
			*pszCookie2=0;
			pszCookie2[1]=0;
			pszCookie=pszCookie2+2;
			pszCookie2=strchr(pszCookie,';');
		} else {
			*pszCookie=0;
			if (pszCookie=strchr(pszCookie+1,';')) {
				*pszCookie=0;
				pszCookie[1]=0;
				pszCookie+=2;
				pszCookie2=strchr(pszCookie,';');
			} else
				break;
		}
	}
}

#if 0 // Web1
bool CLibWebQQ::SendQuery() {
	BOOL fRetry=false;
	LPSTR pszLocalBuffer=NULL;

	while (true) {
		HINTERNET hInetConnect=InternetConnectA(m_hInet,"web-proxy2.qq.com",INTERNET_DEFAULT_HTTP_PORT,NULL,NULL,INTERNET_SERVICE_HTTP,0,NULL);
		m_processstatus=1;
		if (!hInetConnect) {
			SetStatus(WEBQQ_STATUS_ERROR);
			return false;
		}

		m_processstatus=2;
		m_hInetRequest=HttpOpenRequestA(hInetConnect,"POST","/conn_s",NULL,g_referer_webproxy,NULL,/*INTERNET_FLAG_NO_CACHE_WRITE|*/INTERNET_FLAG_PRAGMA_NOCACHE|INTERNET_FLAG_RELOAD,NULL);
		if (!m_hInetRequest) {
			Log(__FUNCTION__"(): HttpOpenRequestA() failed");
			if (true /*!fRetry*/) {
				Log(__FUNCTION__"(): Retry connection");
				fRetry=TRUE;
				continue;
			} else {
				Log(__FUNCTION__"(): Terminate connection");
				SetStatus(WEBQQ_STATUS_ERROR);
				return false;
			}
		}

		m_processstatus=3;
		if (!pszLocalBuffer) {
			pszLocalBuffer=(LPSTR)LocalAlloc(LMEM_FIXED,strlen(m_outbuffer)+1);
			strcpy(pszLocalBuffer,m_outbuffer);
			*m_outbuffer=0;
			m_outbuffer_current=m_outbuffer;
		}
		Log("> %s",pszLocalBuffer);

		m_processstatus=4;
		DWORD dwRead=30000;
		InternetSetOption(m_hInetRequest,INTERNET_OPTION_RECEIVE_TIMEOUT,&dwRead,sizeof(DWORD));

		if (!(HttpSendRequestA(m_hInetRequest,"X-Requested-From: webqq_client",-1,pszLocalBuffer,(DWORD)strlen(pszLocalBuffer)))) {
			DWORD err=GetLastError();
			Log(__FUNCTION__"(): HttpSendRequestA() failed, reason=%d",err);
			InternetCloseHandle(m_hInetRequest);
			m_hInetRequest=NULL;
			InternetCloseHandle(hInetConnect);
			if (err==12002/* && !fRetry*/) {
				// 12002=timeout
				Log(__FUNCTION__"(): Timeout: Retry connection");
				fRetry=TRUE;
				continue;
			} else if (err==12152 && !fRetry) {
				Log(__FUNCTION__"(): WSE12512: Retry connection");
				fRetry=TRUE;
				continue;
			} else if (*m_outbuffer) {
				Log(__FUNCTION__"(): m_outbuffer is not empty, let it send :)");
				*m_buffer=0;
				return true;
			} else {
				Log(__FUNCTION__"(): Terminate connection");
				SetStatus(WEBQQ_STATUS_ERROR);
				if (pszLocalBuffer) LocalFree(pszLocalBuffer);
				return false;
			}
		}

		dwRead=0;
		LPSTR pszBuffer=m_buffer;

		m_processstatus=5;
		while (InternetReadFile(m_hInetRequest,pszBuffer,BUFFERSIZE-1,&dwRead)==TRUE && dwRead>0) {
			pszBuffer+=dwRead;
			dwRead=0;
		}
		*pszBuffer=0;
		Log("< (%d) %s",pszBuffer-m_buffer,m_buffer);

		m_processstatus=6;
		InternetCloseHandle(m_hInetRequest);
		m_hInetRequest=NULL;
		InternetCloseHandle(hInetConnect);
		if (pszLocalBuffer) LocalFree(pszLocalBuffer);

		return true;
	}
}

bool CLibWebQQ::ParseResponse() {
	RECEIVEPACKETINFO rpi;
	CHAR szSplitSignature[16]=";\x1d";
	LPSTR pszNext, pszNext2;
	LPSTR pszCurrent=m_buffer;
	int turn=0;
	bool breakloop=false;
	m_processstatus=7;
	ultoa(m_qqid,szSplitSignature+2,10);

	/*
	*m_outbuffer=0;
	m_outbuffer_current=m_outbuffer;
	*/

	if (*m_buffer) {
		do {
			m_processstatus=8;
			if (pszNext=strstr(pszCurrent,szSplitSignature)) {
				pszNext+=2;
				pszNext[-1]=0;
			}

			Log("<- %s",pszCurrent);
			turn=0;
			
			m_processstatus=9;

			while (*pszCurrent) {
				if (pszNext2=strchr(pszCurrent,';')) 
					*pszNext2=0;
				else
					Log("Warning: Incomplete commands detected. Parsing may crash!");

				if (turn==0)
					rpi.qqid=strtoul(pszCurrent,NULL,10);
				else if (turn==1)
					rpi.cmd=strtoul(pszCurrent,NULL,16);
				else if (turn==2)
					rpi.seq=strtoul(pszCurrent,NULL,10);
				else if (turn==3)
					rpi.args=pszCurrent;

				turn++;
				pszCurrent+=strlen(pszCurrent)+1;
				if (!pszNext2) break;
			}

			pszCurrent=pszNext;

			if (rpi.qqid!=m_qqid) {
				Log(__FUNCTION__"(): ERROR: QQID Mismatch! (%u != %u) Packet Dropped.\n",rpi.qqid,m_qqid);
			} else {
				map<DWORD,LPWEBQQ_OUT_PACKET>::iterator iter=m_outpackets.find(rpi.seq);
				m_processstatus=13;
				//if (LPWEBQQ_OUT_PACKET lpOP=m_outpackets[rpi.seq]) {
				if (iter!=m_outpackets.end()) {
					LPWEBQQ_OUT_PACKET lpOP=iter->second;
					if (lpOP->type!=rpi.cmd) {
						Log(__FUNCTION__"(): WARNING: Packet with same seq but different cmd detected! Ignored OP removal.\n");
					} else {
						LocalFree(lpOP->cmd);
						LocalFree(lpOP);
						m_outpackets.erase(rpi.seq);
					}
				}

				m_processstatus=10;

				switch (rpi.cmd) {
					case 0x22: sc0x22_onSuccLoginInfo(rpi.args); break;
					case WEBQQ_CMD_GET_GROUP_INFO: sc0x3c_onSuccGroupInfo(rpi.args); break;
					case WEBQQ_CMD_GET_LIST_INFO: sc0x58_onSuccListInfo(rpi.args); break;
					case WEBQQ_CMD_GET_NICK_INFO: sc0x26_onSuccNickInfo(rpi.args); break;
					case WEBQQ_CMD_GET_REMARK_INFO: sc0x3e_onSuccRemarkInfo(rpi.args); break;
					case WEBQQ_CMD_GET_HEAD_INFO: sc0x65_onSuccHeadInfo(rpi.args); break;
					case WEBQQ_CMD_GET_CLASS_SIG_INFO: sc0x1d_onSuccGetQunSigInfo(rpi.args); break;
					case WEBQQ_CMD_CLASS_DATA: sc0x30_onClassData(rpi.args, &rpi); break;
					case WEBQQ_CMD_GET_MESSAGE: breakloop=sc0x17_onIMMessage(rpi.seq,rpi.args); break;
					case WEBQQ_CMD_GET_LEVEL_INFO: sc0x5c_onSuccLevelInfo(rpi.args); break;
					case 0x67: sc0x67_onSuccLongNickInfo(rpi.args); break;
					case WEBQQ_CMD_GET_USER_INFO: sc0x06_onSuccUserInfo(rpi.args); break;
					case WEBQQ_CMD_CONTACT_STATUS: sc0x81_onContactStatus(rpi.args); break;
					case WEBQQ_CMD_GET_CLASS_MEMBER_NICKS: sc0x0126_onSuccMemberNickInfo(rpi.args); break;
					case WEBQQ_CMD_RESET_LOGIN: sc0x01_onResetLogin(rpi.args); break;
					case WEBQQ_CMD_SYSTEM_MESSAGE: sc0x80_onSystemMessage(rpi.args); break;

					case WEBQQ_CMD_SEND_C2C_MESSAGE_RESULT:
						Log("WEBQQ_CMD_SEND_C2C_MESSAGE_RESULT");
					case WEBQQ_CMD_SET_STATUS_RESULT:
						if (!strcmp(rpi.args,"0")) {
							SendToHub(0xffff0000+rpi.cmd,rpi.args,&rpi.seq);
						}
						break;
				}
				
				m_processstatus=12;
				SendToHub(0xfffff000+rpi.cmd,rpi.args);
			}
		} while (!breakloop&&pszNext);
	} else if (!m_storage[WEBQQ_STORAGE_PARAMS]) {
		Log(__FUNCTION__"(): Empty CS0x22 reply, send again");
		AppendQuery(cs0x22_getLoginInfo);
		return false;
	} else {
		// Check packet
		LPWEBQQ_OUT_PACKET lpOP;

		for (map<DWORD,LPWEBQQ_OUT_PACKET>::iterator iter=m_outpackets.begin(); iter!=m_outpackets.end();) {
			lpOP=iter->second;
			if (lpOP) {
				if (GetTickCount()-lpOP->created>=5000) {
					if (lpOP->retried>=4) {
						Log(__FUNCTION__"(): Timed out sending 0x%02x packet %u, dropped.",lpOP->type,iter->first);
						LocalFree(lpOP->cmd);
						LocalFree(lpOP);
						iter=m_outpackets.erase(iter);
					} else {
						lpOP->retried++;
						Log(__FUNCTION__"(): Retry sending 0x%02x packet %u for %d time.",lpOP->type,iter->first,lpOP->retried);
						if (*m_outbuffer) m_outbuffer_current+=strlen(strcpy(m_outbuffer_current,"\x1d"));
						m_outbuffer_current+=strlen(strcpy(m_outbuffer_current,lpOP->cmd));
						iter++;
					}
				} else
					iter++;
			} else {
				Log(__FUNCTION__"(): ASSERT NULL outpacket seq %u",iter->first);
				iter=m_outpackets.erase(iter);
			}
		}
	}

	if (!breakloop) {
		if (!m_storage[WEBQQ_STORAGE_PARAMS]) {
			SetStatus(WEBQQ_STATUS_ERROR);
		} else {
			if (cs_0x1d_next_time>0 && GetTickCount()>cs_0x1d_next_time) {
				AppendQuery(cs0x1d_getQunSigInfo);
			}
			AppendQuery(cs0x00_keepAlive);
		}
	}

	return breakloop;
}

LPSTR CLibWebQQ::DecodeText(LPSTR pszText) {
	LPSTR pszRet=pszText;
	char szTemp[3];
	szTemp[2]=0;

	while (pszText=strchr(pszText,'%')) {
		strncpy(szTemp,pszText+1,2);
		*pszText=(char)strtoul(szTemp,NULL,16);
		// if (*pszText==13) *pszText=10;
		pszText++;
		memmove(pszText,pszText+2,strlen(pszText+1));
	}

	return pszRet;
}

void CLibWebQQ::SetOnlineStatus(WEBQQPROTOCOLSTATUSENUM newstatus) {
	char szTemp[16];
	AppendQuery(cs0x0d_setStatus,itoa(newstatus,szTemp,10));
	AttemptSendQueue();
}

void CLibWebQQ::GetClassMembersRemarkInfo(DWORD qunid) {
	char szTemp[32];
	sprintf(szTemp,"%u;0;0",qunid);
	AppendQuery(cs0x30_getMemberRemarkInfo,szTemp);
}

void CLibWebQQ::GetNickInfo(LPDWORD qqid) {
	char szTemp[320];
	int c=0;
	LPSTR pszTemp=szTemp+20;

	while (qqid[c]) {
		pszTemp+=strlen(ultoa(qqid[c],pszTemp,10));
		*pszTemp++=';';
		c++;
		if (c>=24) break;
	}
	pszTemp[-1]=0;

	itoa(c,szTemp,10);
	strcat(szTemp,";");
	memmove(szTemp+strlen(szTemp),szTemp+20,strlen(szTemp+20)+1);
	AppendQuery(cs0x0126_getMemberNickInfo,szTemp);
}

LPSTR CLibWebQQ::EncodeText(LPCSTR pszSrc, LPSTR pszDst) {
	while (*pszSrc) {
		if (*pszSrc==' ' || *pszSrc=='%' || *pszSrc=='\\' || *pszSrc=='\'' || *pszSrc=='/' || *pszSrc==' ') {
			*pszDst++='%';
			pszDst+=sprintf(pszDst,"%02X",(int)*(LPBYTE)pszSrc);
		} else
			*pszDst++=*pszSrc;

		pszSrc++;
	}
	*pszDst=0;
	
	return pszDst;
}

DWORD CLibWebQQ::SendContactMessage(DWORD qqid, LPCSTR message, int fontsize, LPSTR font, DWORD color, BOOL bold, BOOL italic, BOOL underline) {
	LPSTR pszBuffer=(LPSTR)LocalAlloc(LMEM_FIXED,10+17+strlen(font)*3+strlen(message)*3);
	LPSTR ppszBuffer=pszBuffer;
	DWORD seq;

	ppszBuffer+=strlen(ultoa(qqid,ppszBuffer,10));
	ppszBuffer+=sprintf(ppszBuffer,";0b;%d;",(strchr(message,0x15)!=NULL && strchr(message,0x1f)!=NULL)?252:0);
	
	ppszBuffer=EncodeText(message,ppszBuffer);
	*ppszBuffer++=';';

	BYTE attr=fontsize&31;
	if (bold) attr|=32;
	if (italic) attr|=64;
	if (underline) attr|=128;
	ppszBuffer+=sprintf(ppszBuffer,"%02x",(int)attr);
	ppszBuffer+=sprintf(ppszBuffer,"%06x",color);
	ppszBuffer+=sprintf(ppszBuffer,"10");
	ppszBuffer=EncodeText(font,ppszBuffer);
	seq=AppendQuery(cs0x16_sendMessage,pszBuffer);
	LocalFree(pszBuffer);
	AttemptSendQueue();
	return seq;
}
#endif // Web1

DWORD CLibWebQQ::SendContactMessage(DWORD qqid, WORD face, bool hasImage, JSONNODE* jnContent) {
	JSONNODE* jn=json_new(JSON_NODE);
	DWORD dwSize;
	LPSTR pszData;
	JSONNODE* jn2;
	DWORD dwRet=0;
	json_push_back(jn,json_new_f("to",qqid));
	json_push_back(jn,json_new_i("face",face));

	/*
	if (hasImage) {
		JSONNODE* jnResult=NULL;

		if (m_web2_nextqunkey==0 || time(NULL)<=m_web2_nextqunkey) {
			char szQuery[100]; // Typically only ~75 bytes
			sprintf(szQuery,"http://web2-b.qq.com/channel/get_gface_sig?clientid=%s&t=%u%u",m_web2_clientid,(DWORD)time(NULL),GetTickCount()%100);

			pszData=GetHTMLDocument(szQuery,g_referer_web2,&dwSize);
			if (pszData!=NULL && dwSize!=0xffffffff && *pszData=='{') {
				jn2=json_parse(pszData);

				if (json_as_int(json_get(jn2,"retcode"))!=0) {
					Log(__FUNCTION__"(): Get Qun key failed: bad retcode");
					jnContent=NULL;
				} else {
					jnResult=json_get(jn2,"result");
					if (json_as_int(json_get(jn2,"reply"))!=0) {
						Log(__FUNCTION__"(): Get Qun key failed: bad reply");
						jnContent=NULL;
					} else {
						if (m_web2_storage[WEBQQ_WEB2_STORAGE_QUNKEY]) json_delete(m_web2_storage[WEBQQ_WEB2_STORAGE_QUNKEY]);
						m_web2_storage[WEBQQ_WEB2_STORAGE_QUNKEY]=jn2;
					}
				}

				LocalFree(pszData);
			}
		} else
			jnResult=json_get(m_web2_storage[WEBQQ_WEB2_STORAGE_QUNKEY],"result");

		if (jnResult) {
			LPSTR pszTemp;
			json_push_back(jn,json_new_f("group_code",extid));
			json_push_back(jn,json_new_a("key",pszTemp=json_as_string(json_get(jnResult,"gface_key"))));
			json_free(pszTemp);
			json_push_back(jn,json_new_a("sig",pszTemp=json_as_string(json_get(jnResult,"gface_sig"))));
			json_free(pszTemp);
		}
	}
	*/

	if (jnContent) {
		LPSTR szContent=json_write(jnContent);
		json_push_back(jn,json_new_a("content",szContent));
		json_push_back(jn,json_new_f("msg_id",++m_sequence));
		json_push_back(jn,json_new_f("clientid",strtoul(m_web2_clientid,NULL,10)));
		json_push_back(jn,json_new_a("psessionid",m_web2_psessionid));

		LPSTR szJSON=json_write(jn);
		LPSTR pszContent=(LPSTR)LocalAlloc(LMEM_FIXED,strlen(szJSON)+3);

		strcat(strcpy(pszContent,"r="),szJSON);
		
		if ((pszData=PostHTMLDocument(g_server_web2_connection,"/channel/send_msg",GetReferer(WEBQQ_REFERER_WEB2PROXY),pszContent,&dwSize))!=NULL && dwSize!=0xffffffff && *pszData=='{') {
			jn2=json_parse(pszData);
			if (json_as_int(json_get(jn2,"retcode"))==0) 
				dwRet=m_sequence;
			else
				Log(__FUNCTION__"(): Send Contact Msg Failed: %s",pszData);

			LocalFree(pszData);
			json_delete(jn2);
		} else
			Log(__FUNCTION__"(): Send Qun Contact Failed: Connection Failed(%d)",GetLastError());

		LocalFree(pszContent);
		json_free(szJSON);
		json_free(szContent);
	}

	json_delete(jn);
	// json_delete(jnContent); - This left to caller

	return dwRet;
}

DWORD CLibWebQQ::SendClassMessage(DWORD intid, DWORD extid, bool hasImage, JSONNODE* jnContent) {
	JSONNODE* jn=json_new(JSON_NODE);
	DWORD dwSize;
	LPSTR pszData;
	JSONNODE* jn2;
	DWORD dwRet=0;
	json_push_back(jn,json_new_f("group_uin",intid));

	if (hasImage) {
		JSONNODE* jnResult=NULL;

		if (m_web2_nextqunkey==0 || time(NULL)<=m_web2_nextqunkey) {
			char szQuery[384]; // Typically only ~75 bytes
			sprintf(szQuery,"http://web2-b.qq.com/channel/get_gface_sig?clientid=%s&psessionid=%s&t=%u%u",m_web2_clientid,m_web2_psessionid,(DWORD)time(NULL),GetTickCount()%100);

			pszData=GetHTMLDocument(szQuery,g_referer_web2,&dwSize);
			if (pszData!=NULL && dwSize!=0xffffffff && *pszData=='{') {
				jn2=json_parse(pszData);

				if (json_as_int(json_get(jn2,"retcode"))!=0) {
					Log(__FUNCTION__"(): Get Qun key failed: bad retcode");
					jnContent=NULL;
				} else {
					jnResult=json_get(jn2,"result");
					if (json_as_int(json_get(jnResult,"reply"))!=0) {
						Log(__FUNCTION__"(): Get Qun key failed: bad reply");
						jnContent=NULL;
					} else {
						if (m_web2_storage[WEBQQ_WEB2_STORAGE_QUNKEY]) json_delete(m_web2_storage[WEBQQ_WEB2_STORAGE_QUNKEY]);
						m_web2_storage[WEBQQ_WEB2_STORAGE_QUNKEY]=jn2;
					}
				}

				LocalFree(pszData);
			}
		} else
			jnResult=json_get(m_web2_storage[WEBQQ_WEB2_STORAGE_QUNKEY],"result");

		if (jnResult) {
			LPSTR pszTemp;
			json_push_back(jn,json_new_f("group_code",extid));
			json_push_back(jn,json_new_a("key",pszTemp=json_as_string(json_get(jnResult,"gface_key"))));
			json_free(pszTemp);
			json_push_back(jn,json_new_a("sig",pszTemp=json_as_string(json_get(jnResult,"gface_sig"))));
			json_free(pszTemp);
		}
	}

	if (jnContent) {
		LPSTR szContent=json_write(jnContent);
		json_push_back(jn,json_new_a("content",szContent));
		json_push_back(jn,json_new_f("msg_id",++m_sequence));
		json_push_back(jn,json_new_f("clientid",strtoul(m_web2_clientid,NULL,10)));
		json_push_back(jn,json_new_a("psessionid",m_web2_psessionid));

		LPSTR szJSON=json_write(jn);
		LPSTR pszContent=(LPSTR)LocalAlloc(LMEM_FIXED,strlen(szJSON)+3);

		strcat(strcpy(pszContent,"r="),szJSON);
		
		if ((pszData=PostHTMLDocument(g_server_web2_connection,"/channel/send_group_msg",GetReferer(WEBQQ_REFERER_WEB2PROXY),pszContent,&dwSize))!=NULL && dwSize!=0xffffffff && *pszData=='{') {
			jn2=json_parse(pszData);
			if (json_as_int(json_get(jn2,"retcode"))==0) 
				dwRet=m_sequence;
			else
				Log(__FUNCTION__"(): Send Qun Msg Failed: %s",pszData);

			LocalFree(pszData);
			json_delete(jn2);
		} else
			Log(__FUNCTION__"(): Send Qun Msg Failed: Connection Failed(%d)",GetLastError());

		LocalFree(pszContent);
		json_free(szJSON);
		json_free(szContent);
	}

	json_delete(jn);
	// json_delete(jnContent); - This left to caller

	return dwRet;
}

#if 0 // Web1
DWORD CLibWebQQ::SendClassMessage(DWORD qunid, LPCSTR message, int fontsize, LPSTR font, DWORD color, BOOL bold, BOOL italic, BOOL underline) {
	LPSTR pszBuffer=(LPSTR)LocalAlloc(LMEM_FIXED,10+17+strlen(font)*3+strlen(message)*3);
	LPSTR ppszBuffer=pszBuffer;
	DWORD seq;

	ppszBuffer+=sprintf(ppszBuffer,"%u;",qunid);
	
	ppszBuffer=EncodeText(message,ppszBuffer);
	*ppszBuffer++=';';

	BYTE attr=fontsize&31;
	if (bold) attr|=32;
	if (italic) attr|=64;
	if (underline) attr|=128;
	ppszBuffer+=sprintf(ppszBuffer,"%02x",(int)attr);
	ppszBuffer+=sprintf(ppszBuffer,"%06x",color);
	ppszBuffer+=sprintf(ppszBuffer,"10");
	ppszBuffer=EncodeText(font,ppszBuffer);
	seq=AppendQuery(cs0x30_sendClassMessage,pszBuffer);
	LocalFree(pszBuffer);
	AttemptSendQueue();
	return seq;
}

void CLibWebQQ::AddFriendPassive(DWORD qqid, LPSTR message) {
	LPSTR pszSuspect=message;
	char szCmd[100];

	while (pszSuspect=strchr(pszSuspect,';')) *pszSuspect='_'; // Dirty workaround

	sprintf(szCmd,"2;%u;1;0;%s",qqid,message);
	AppendQuery(cs0xa8_addFriendPassive,szCmd);
	AttemptSendQueue();
}

void CLibWebQQ::AttemptSendQueue() {
	if (m_hInetRequest) {
		Log(__FUNCTION__"(): Attempt to interrupt conn_s");
		InternetCloseHandle(m_hInetRequest);
	} else {
		Log(__FUNCTION__"(): conn_s is not waiting");
	}
}
#endif // Web1

int CLibWebQQ::FetchUserHead(DWORD qqid, WEBQQUSERHEADENUM uhtype, LPSTR saveto) {
	// Return 0=Fail, 1=BMP, 2=GIF, 3=JPG/Unknown
	// var face_server_domain = "http://face%id%.qun.qq.com";
	// k.img.src = face_server_domain.replace("%id%", j.uin % 10 + 1) + "/cgi/svr/face/getface?type=1&me=" + this.uin + "&uin=" + j.uin
	char szUrl[MAX_PATH];
	DWORD dwSize;
	LPSTR pszFile;
	
	sprintf(szUrl,"http://face%d.qun.qq.com/cgi/svr/face/getface?cache=1&type=%d&fid=0&uin=%u&vfwebqq=%s&t=%u%03d",qqid%10+1,uhtype,qqid,m_web2_vfwebqq,(DWORD)time(NULL),GetTickCount());
	pszFile=GetHTMLDocument(szUrl,g_referer_main,&dwSize);

	if (dwSize!=(DWORD)-1 && dwSize>0 && pszFile!=NULL) {
		int format=memcmp(pszFile,"BM",2)?memcmp(pszFile,"GIF",3)?3:2:1;

		strcpy(strrchr(saveto,'.')+1,format==1?"bmp":format==2?"gif":"jpg");

		HANDLE hFile=CreateFileA(saveto,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,CREATE_ALWAYS,0,NULL);
		if (hFile!=INVALID_HANDLE_VALUE) {
			DWORD dwWritten;
			WriteFile(hFile,pszFile,dwSize,&dwWritten,NULL);
			CloseHandle(hFile);
			LocalFree(pszFile);
			return format;
		}
		LocalFree(pszFile);
	} else
		Log(__FUNCTION__"(): Invalid response data");

	return 0;
}

#if 0 // Web1
void CLibWebQQ::GetLongNames(int count, LPDWORD qqs) {
	char szSend[MAX_PATH];
	LPSTR pszSend=szSend+sprintf(szSend,"%d;",count);
	for (int c=0; c<count; c++) {
		pszSend+=sprintf(pszSend,"%u;",qqs[c]);
	}
	pszSend[-1]=0;
	AppendQuery(cs0x67_getLongNickInfo,szSend);
}

void CLibWebQQ::SendP2PRetrieveRequest(DWORD qqid, LPCSTR type) {
	char szBuffer[32];
	LPSTR ppszBuffer=szBuffer;

	ppszBuffer+=strlen(ultoa(qqid,ppszBuffer,10));
	ppszBuffer+=sprintf(ppszBuffer,";81;%s",type);
	AppendQuery(cs0x16_sendMessage,szBuffer);
	AttemptSendQueue();
}
#endif // Web1

DWORD CLibWebQQ::ReserveSequence() {
	return m_sequence++;
}

LPSTR CLibWebQQ::GetStorage(int index) {
	return m_storage[index];
}

#define BOUNDARY "----WebKitFormBoundaryzx79ypAk2ot8p91p"

void CLibWebQQ::UploadQunImage(HANDLE hFile, LPSTR pszFilename, DWORD respondid) {
	DWORD filelen=GetFileSize(hFile,NULL);
	LPSTR pszBuffer=(LPSTR)LocalAlloc(LMEM_FIXED,filelen+1024);
	int len;
	LPSTR pszSuffix=strrchr(pszFilename,'.');
	WEBQQ_QUNUPLOAD_STATUS wqs={respondid};

	
	HINTERNET hConn=InternetConnectA(m_hInet,"web.qq.com",INTERNET_DEFAULT_HTTP_PORT,NULL,NULL,INTERNET_SERVICE_HTTP,0,NULL);
	HINTERNET hRequest=HttpOpenRequestA(hConn,"POST","/cgi-bin/cface_upload",NULL,NULL,NULL,INTERNET_FLAG_PRAGMA_NOCACHE|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_RELOAD,NULL);
	/*
	HINTERNET hConn=InternetConnectA(m_hInet,"127.0.0.1",INTERNET_DEFAULT_HTTP_PORT,NULL,NULL,INTERNET_SERVICE_HTTP,0,NULL);
	HINTERNET hRequest=HttpOpenRequestA(hConn,"POST","/posttest.php",NULL,NULL,NULL,INTERNET_FLAG_PRAGMA_NOCACHE|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_RELOAD,NULL);
	*/

	// HttpAddRequestHeadersA(hRequest,"Accept: application/xml,application/xhtml+xml,text/html;q=0.9,text/plain;q=0.8,image/png,*/*;q=0.5",-1,HTTP_ADDREQ_FLAG_REPLACE|HTTP_ADDREQ_FLAG_ADD);
	HttpAddRequestHeadersA(hRequest,"Content-Type: multipart/form-data; boundary=" BOUNDARY,-1,HTTP_ADDREQ_FLAG_REPLACE|HTTP_ADDREQ_FLAG_ADD);
	/*
	HttpAddRequestHeadersA(hRequest,"Origin: http://webqq.qq.com",-1,HTTP_ADDREQ_FLAG_REPLACE|HTTP_ADDREQ_FLAG_ADD);
	*/
	HttpAddRequestHeadersA(hRequest,"Referer: http://webqq.qq.com/main.shtml?direct__2",-1,HTTP_ADDREQ_FLAG_REPLACE|HTTP_ADDREQ_FLAG_ADD);
	// HttpAddRequestHeadersA(hRequest,"User-Agent: Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.99 Safari/533.4",-1,HTTP_ADDREQ_FLAG_REPLACE|HTTP_ADDREQ_FLAG_ADD);


	len=sprintf(pszBuffer,"--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"custom_face\"; filename=\"%s\"\r\nContent-Type: image/%s\r\nContent-Transfer-Encoding: binary\r\n\r\n",pszFilename,(pszSuffix!=NULL && !stricmp(pszSuffix,".gif"))?"gif":"jpeg");
	// Log("ASSERT: len=%d, pszBuffer=%s",len,pszBuffer);
	// HttpSendRequestA(hRequest,NULL,0,pszBuffer,len);

	DWORD dwRead;
	wqs.status=0;
	wqs.number=filelen;
	SendToHub(WEBQQ_CALLBACK_QUNIMGUPLOAD,NULL,&wqs);
	// len=0;

	wqs.status=1;

	/*
	while (ReadFile(hFile,pszBuffer,16384,&dwRead,NULL) && dwRead>0) {
		HttpSendRequestA(hRequest,NULL,0,pszBuffer,dwRead);
		len+=dwRead;
		wqs.number=len;
		SendToHub(WEBQQ_CALLBACK_QUNIMGUPLOAD,NULL,&wqs);
	}
	*/
	ReadFile(hFile,pszBuffer+len,filelen,&dwRead,NULL);
	CloseHandle(hFile);

	len+=filelen;

	len+=sprintf(pszBuffer+len,"\r\n--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"from\"\r\n\r\ncontrol");
	if (m_useweb2)
		len+=sprintf(pszBuffer+len,"\r\n--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\nEQQ.Model.ChatMsg.callbackSendPicGroup");
	else
		len+=sprintf(pszBuffer+len,"\r\n--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\nWEBQQ.obj.QQClient.mainPanel._picSendCallback");
	len+=sprintf(pszBuffer+len,"\r\n--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"fileid\"\r\n\r\n1");
	len+=sprintf(pszBuffer+len,"\r\n--" BOUNDARY "--");
	HttpSendRequestA(hRequest,NULL,0,pszBuffer,len);
	
	wqs.status=2;
	wqs.number=0;
	SendToHub(WEBQQ_CALLBACK_QUNIMGUPLOAD,NULL,&wqs);

	DWORD dwWritten=sizeof(DWORD);
	DWORD dwValue=0;
	
	HttpQueryInfo(hRequest,HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER,&dwValue,&dwWritten,&dwRead);
	Log(__FUNCTION__"() Status=%d",dwValue);

	InternetReadFile(hRequest,pszBuffer,16384,&dwRead);
	pszBuffer[dwRead]=0;
	InternetCloseHandle(hRequest);
	InternetCloseHandle(hConn);

	// <head><script type="text/javascript">document.domain='qq.com';parent.WEBQQ.obj.QQClient.mainPanel._picSendCallback({'ret':0,'msg':'219C9C18D0C5A676899CD8595507CFCE.gIf'});</script></head><body></body>
	LPSTR ppszBuffer=strstr(pszBuffer,"'ret':");
	if (ppszBuffer!=NULL) {
		if (ppszBuffer[6]=='0') {
			ppszBuffer=strstr(ppszBuffer,"'msg':'")+7;
			*strstr(ppszBuffer,"'}")=0;
			wqs.status=3;
			wqs.string=ppszBuffer;
		} else if (ppszBuffer[6]=='4') {
			// Most likely already uploaded
			ppszBuffer=strstr(ppszBuffer,"'msg':'")+7;
			*strstr(ppszBuffer," -")=0;
			wqs.status=3;
			wqs.string=ppszBuffer;
		}
	} else {
		wqs.status=4;
		wqs.string=pszBuffer;
	}
	SendToHub(WEBQQ_CALLBACK_QUNIMGUPLOAD,NULL,&wqs);
	LocalFree(pszBuffer);
}

void CLibWebQQ::SetBasePath(LPCSTR pszPath) {
	m_basepath=strdup(pszPath);
}

#if 0 // Web1
void CLibWebQQ::GetGroupInfo() {
	AppendQuery(cs0x3c_getGroupInfo);
}

/** CS **/

DWORD cs0x22_getLoginInfo(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	if (!protocol) return 0x22;
	if (!pszOutBuffer) return 1; // Need ack
	return sprintf(pszOutBuffer,"%s;%s;%d",protocol->GetCookie("skey"),protocol->GetCookie("ptwebqq"),protocol->GetStorage(WEBQQ_STORAGE_PARAMS)==NULL?0:1);
}

DWORD cs0x3c_getGroupInfo(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	if (!protocol) return 0x3c;
	if (!pszOutBuffer) return 1; // Need ack
	return (DWORD)strlen(strcpy(pszOutBuffer,"1"/* 2 is possible if !!(strtoul(pszArgs))!=0 */));
}

DWORD cs0x00_keepAlive(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	return 0;
}

DWORD cs0x06_getUserInfo(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	if (!protocol) return 0x06;
	if (!pszOutBuffer) return 1; // Need ack
	return sprintf(pszOutBuffer,"%u",protocol->GetQQID());
}

DWORD cs0x5c_getLevelInfo(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	if (!protocol) return 0x5c;
	if (!pszOutBuffer) return 1; // Need ack
	return (DWORD)strlen(strcpy(pszOutBuffer,"88"));
}

DWORD cs0x67_getLongNickInfo(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	// Args: count;id1;id2;...
	if (!protocol) return 0x67;
	if (!pszOutBuffer) return 1; // Need ack
	return sprintf(pszOutBuffer,"03;%s",pszArgs);
}

DWORD cs0x58_getListInfo(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	// Args:  cs_0x58_next_uin
	if (!protocol) return 0x58;
	if (!pszOutBuffer) return 1; // Need ack
	return (DWORD)strlen(strcpy(pszOutBuffer,pszArgs));
}

DWORD cs0x26_getNickInfo(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	// Args: cs_0x26_next_pos, cs_0x26_timeout++
	if (!protocol) return 0x26;
	if (!pszOutBuffer) return 1; // Need ack
	return (DWORD)strlen(strcpy(pszOutBuffer,pszArgs));
}

DWORD cs0x3e_getRemarkInfo(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	// Args: cs_0x3e_next_pos
	if (!protocol) return 0x3e;
	if (!pszOutBuffer) return 1; // Need ack
	return sprintf(pszOutBuffer,"4;%s",pszArgs);
}

DWORD cs0x65_getHeadInfo(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	if (!protocol) return 0x65;
	if (!pszOutBuffer) return 1; // Need ack
	return sprintf(pszOutBuffer,"02;%u",protocol->GetQQID());
}

DWORD cs0x1d_getQunSigInfo(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	if (!protocol) return 0x1d;
	if (!pszOutBuffer) return 1; // Need ack
	return 0;
}

DWORD cs0x30_getClassInfo(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	// Use when member info not get
	// Args: qunid;msg;font
	if (!protocol) return 0x30;
	if (!pszOutBuffer) return 1; // Need ack
	return sprintf(pszOutBuffer,"72;%s",pszArgs);
}

DWORD cs0x17_sendMessageAck(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	// Use when member info not get
	// Args: msgseq;session;qunid;qqid;seq;type;tail
	if (!protocol) return 0x17;
	if (!pszOutBuffer) return 0; // No ack required
	return (DWORD)strlen(strcpy(pszOutBuffer,pszArgs));
}

DWORD cs0x30_getMemberRemarkInfo(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	// Args: qunid;0;next_pos
	if (!protocol) return 0x30;
	if (!pszOutBuffer) return 1; // Need ack
	return sprintf(pszOutBuffer,"0f;%s",pszArgs);
}

DWORD cs0x30_sendClassMessage(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	// Args: qunid;0;next_pos
	if (!protocol) return 0x30;
	if (!pszOutBuffer) return 0; // No ack required
	return sprintf(pszOutBuffer,"0a;%s",pszArgs);
}

DWORD cs0x0126_getMemberNickInfo(CLibWebQQ* protocol, LPSTR pszOutBuffer, LPSTR pszArgs) {
	// Args: count(<=24);qqid;qqid;...
	if (!protocol) return 0x0126;
	if (!pszOutBuffer) 