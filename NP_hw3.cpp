#include <windows.h>
#include <list>
using namespace std;

#include "resource.h"

#define SERVER_PORT 7799
#define BUF_SIZE 6000

#define WM_SOCKET_NOTIFY (WM_USER + 1)

BOOL CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
int EditPrintf (HWND, TCHAR *, ...);
void parse_query_string(char *querystring);

//=================================================================
//	Global Variables
//=================================================================
list<SOCKET> Socks;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	
	return DialogBox(hInstance, MAKEINTRESOURCE(ID_MAIN), NULL, MainDlgProc);
}

char filename[100];
char header[] = "HTTP/1.1 200 OK\nContent-Type: text/html\n\n";
BOOL CALLBACK MainDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	WSADATA wsaData;

	static HWND hwndEdit;
	static SOCKET msock, ssock;
	static struct sockaddr_in sa;

	int err;


	switch(Message) 
	{
		case WM_INITDIALOG:
			hwndEdit = GetDlgItem(hwnd, IDC_RESULT);
			break;
		case WM_COMMAND:
			switch(LOWORD(wParam))
			{
				case ID_LISTEN:

					WSAStartup(MAKEWORD(2, 0), &wsaData);

					//create master socket
					msock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

					if( msock == INVALID_SOCKET ) {
						EditPrintf(hwndEdit, TEXT("=== Error: create socket error ===\r\n"));
						WSACleanup();
						return TRUE;
					}

					err = WSAAsyncSelect(msock, hwnd, WM_SOCKET_NOTIFY, FD_ACCEPT | FD_CLOSE | FD_READ | FD_WRITE);

					if ( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: select error ===\r\n"));
						closesocket(msock);
						WSACleanup();
						return TRUE;
					}

					//fill the address info about server
					sa.sin_family		= AF_INET;
					sa.sin_port			= htons(SERVER_PORT);
					sa.sin_addr.s_addr	= INADDR_ANY;

					//bind socket
					err = bind(msock, (LPSOCKADDR)&sa, sizeof(struct sockaddr));

					if( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: binding error ===\r\n"));
						WSACleanup();
						return FALSE;
					}

					err = listen(msock, 2);
		
					if( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: listen error ===\r\n"));
						WSACleanup();
						return FALSE;
					}
					else {
						EditPrintf(hwndEdit, TEXT("=== Server START ===\r\n"));
					}

					break;
				case ID_EXIT:
					EndDialog(hwnd, 0);
					break;
			};
			break;

		case WM_CLOSE:
			EndDialog(hwnd, 0);
			break;

		case WM_SOCKET_NOTIFY:
			switch( WSAGETSELECTEVENT(lParam) )
			{
				case FD_ACCEPT:
					ssock = accept(msock, NULL, NULL);
					Socks.push_back(ssock);
					EditPrintf(hwndEdit, TEXT("=== Accept one new client(%d), List size:%d ===\r\n"), ssock, Socks.size());
					break;
				case FD_READ:
					OutputDebugString("FD_READ\n");

					//Write your code for read event here.
					char buf[BUF_SIZE];
					int result;
					result = recv(ssock, buf, BUF_SIZE, 0);
					buf[result] = 0;

					if (result <= 0)
						break;

					char *url;
					char *method;
					method = strtok(buf, " ");
					if (strcmp(method, "GET") != 0)
						break;
					url = strtok(NULL, " ");
					EditPrintf(hwndEdit, TEXT("url: %s\r\n"), url);
					
					//GET /test?qq=123 HTTP/1.1

					char *route;
					route = strtok(url, "?");
					strcpy(filename, route + 1);

					char *querystring;
					querystring = strtok(NULL, "");
					EditPrintf(hwndEdit, TEXT("querystring: %s\r\n"), querystring);
					
					parse_query_string(querystring);

					break;
				case FD_WRITE:
					//Write your code for write event here
					OutputDebugString("FD_WRITE\n");
					send(ssock, header, sizeof(header) - 1, 0);
					FILE *pFile;
					long lSize;
					char *buffer;
					size_t file_result;
					OutputDebugString("filename:\n");
					OutputDebugString(filename);
					pFile = fopen(filename, "rb");
					if (pFile == NULL) { OutputDebugString("fopen error\n"); exit(1); }

					// obtain file size:
					fseek(pFile, 0, SEEK_END);
					lSize = ftell(pFile);
					rewind(pFile);
					buffer = (char*)malloc(sizeof(char)*lSize);
					if (buffer == NULL) { OutputDebugString("malloc error\n"); exit(2); }

					file_result = fread(buffer, 1, lSize, pFile);
					if (file_result != lSize) { OutputDebugString("fread error\n"); exit(3); }

					send(ssock, buffer, file_result, 0);

					fclose(pFile);
					free(buffer);

					closesocket(ssock);
					break;
				case FD_CLOSE:

					break;
			};
			break;
		
		default:
			return FALSE;


	};

	return TRUE;
}

int EditPrintf (HWND hwndEdit, TCHAR * szFormat, ...)
{
     TCHAR   szBuffer [1024] ;
     va_list pArgList ;

     va_start (pArgList, szFormat) ;
     wvsprintf (szBuffer, szFormat, pArgList) ;
     va_end (pArgList) ;

     SendMessage (hwndEdit, EM_SETSEL, (WPARAM) -1, (LPARAM) -1) ;
     SendMessage (hwndEdit, EM_REPLACESEL, FALSE, (LPARAM) szBuffer) ;
     SendMessage (hwndEdit, EM_SCROLLCARET, 0, 0) ;
	 return SendMessage(hwndEdit, EM_GETLINECOUNT, 0, 0); 
}

typedef struct {
	char*  ip;
	int    port;
	FILE*  batchfile;
	SOCKET sock;
} server;

void parse_query_string(char *querystring) {
	char* item = strtok(querystring, "&");

	if (item == NULL)
		return;

	while (item != NULL) {
		char* ptr;
		char *key = strtok_s(item, "=", &ptr);
		char *val = strtok_s(NULL, "", &ptr);
		char debug[100];
		sprintf(debug, "%s=%s\n", key, val);
		OutputDebugString(debug);
		item = strtok(NULL, "&");
	}
}