#include <windows.h>
#include <list>
#include <string.h>

using namespace std;

#include "resource.h"

#define SERVER_PORT 7890
#define BUF_SIZE 10000
#define MAX_CLIENT 5
#define WM_SOCKET_HTTP (WM_USER + 1)
#define WM_SOCKET_RAS (WM_USER + 2)
#define HTTP_ACCEPT 0
#define HTTP_READ 1
#define HTTP_WRITE 2
#define HTTP_DONE 3

#define F_CONNECTING 0
#define F_READING 1
#define F_WRITING 2
#define F_DONE 3


typedef struct {
    int     index;
    char*   host;
    UINT16  port;
    FILE*   batch;
    int     sockfd;
    int     status;
    int     dying;
    char*   lastcmd;
	SOCKET  httpsock;
} Client;

typedef struct {
	SOCKET  ssock;
	char    buffer[BUF_SIZE];
	int     status;
	Client* *clients;
	int     nclients;
} HTTPClient;


BOOL CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
int EditPrintf (HWND, TCHAR *, ...);
HTTPClient* get_client(SOCKET ssock);
void bad_request(HTTPClient *client, char *msg);
void home_page(HTTPClient *client);
void not_found(HTTPClient *client);
void serve_file(HTTPClient *client, char *filename, char *content_type);
Client** parse_query_string(char *querystring);

//=================================================================
//	Global Variables
//=================================================================
list<HTTPClient*> httpclients;
Client* *clients;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	
	return DialogBox(hInstance, MAKEINTRESOURCE(ID_MAIN), NULL, MainDlgProc);
}

BOOL CALLBACK MainDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	WSADATA wsaData;

	static HWND hwndEdit;
	static SOCKET msock, ssock, rsock;
	static struct sockaddr_in sa;
	static HTTPClient *client;

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

					err = WSAAsyncSelect(msock, hwnd, WM_SOCKET_HTTP, FD_ACCEPT | FD_CLOSE);

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

		case WM_SOCKET_HTTP:
			switch( WSAGETSELECTEVENT(lParam) )
			{
				case FD_ACCEPT:
					ssock = accept(msock, NULL, NULL);
					err = WSAAsyncSelect(ssock, hwnd, WM_SOCKET_HTTP, FD_READ | FD_WRITE | FD_CLOSE);

					if ( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: select error ===\r\n"));
						closesocket(msock);

						WSACleanup();
						return TRUE;
					}
					
					client = (HTTPClient*)malloc(sizeof(HTTPClient));
					memset(client, 0, sizeof(HTTPClient));
					client->ssock = ssock;
					client->status = HTTP_ACCEPT;
					httpclients.push_back(client);

					EditPrintf(hwndEdit, TEXT("=== Accept one new client(%d), List size:%d ===\r\n"), ssock, httpclients.size());
					break;
				case FD_READ:
					//Write your code for read event here.
					int rResult;

					client = get_client(wParam);
					if (client == NULL)
						break;
					if (client->status != HTTP_ACCEPT)
						break;

					rResult = recv(client->ssock, client->buffer, BUF_SIZE, 0);
					if (rResult < 0) {
						int err = WSAGetLastError();
						if (err == WSAEWOULDBLOCK)
							break;
						EditPrintf(hwndEdit, TEXT("=== Fail to recv (%d) code: 0x%x ===\r\n"), msock, err);
						break;
					}
					client->buffer[rResult] = '\0';
					OutputDebugString(client->buffer);
					client->status = HTTP_WRITE;
				case FD_WRITE:
					//Write your code for write event here
					client = get_client(wParam);
					if (client == NULL)
						break;
					if (client->status != HTTP_WRITE)
						break;

					char *url;
					char *method;
					method = strtok(client->buffer, " ");
            
					if (*method == '\0') {
						bad_request(client, "No method");
						break;
					}
            
					url = strtok(NULL, " ");
            
					if (url == NULL || *url == '\0') {
						bad_request(client, "No url found");
						break;
					}

					//GET /test?qq=123 HTTP/1.1
            
					char *route;
					route = strtok(url, "?");
            
					if (route == NULL || *route == '\0') {
						home_page(client);
						break;
					}
            
					route++; // exclude the first '/' character
            
					if (*route == '\0') {
						home_page(client);
						break;
					}
            
					char *querystring;
					querystring = strtok(NULL, "");

					if (strncmp(route, "hw3.cgi\0", 8) == 0) {
						client->clients = parse_query_string(querystring);
						client->nclients = 0;
						for (int i = 0; i < MAX_CLIENT; i++) {
							if (client->clients[i] == NULL)
								continue;

							//create ras client socket
							rsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

							if( rsock == INVALID_SOCKET ) {
								EditPrintf(hwndEdit, TEXT("=== Error: create socket in ras error ===\r\n"));
								WSACleanup();
								return TRUE;
							}

							err = WSAAsyncSelect(rsock, hwnd, WM_SOCKET_RAS, FD_CONNECT | FD_WRITE | FD_READ | FD_CLOSE);

							if ( err == SOCKET_ERROR ) {
								EditPrintf(hwndEdit, TEXT("=== Error: select error in ras ===\r\n"));
								closesocket(rsock);

								WSACleanup();
								return TRUE;
							}

							struct hostent *he = gethostbyname(client->clients[i]->host);

							//fill the address info about server
							sa.sin_family		= AF_INET;
							sa.sin_port			= htons(client->clients[i]->port);
							sa.sin_addr	        = *((struct in_addr *)he->h_addr);

							err = connect(rsock, (LPSOCKADDR)&sa, sizeof(struct sockaddr));
							if( err == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
								EditPrintf(hwndEdit, TEXT("=== Error: connect error in ras ===\r\n"));
								WSACleanup();
								return FALSE;
							}

							client->clients[i]->sockfd = rsock;
							client->clients[i]->status = F_CONNECTING;
							client->clients[i]->httpsock = client->ssock;
							client->nclients++;
						}
						break;
					}

					char *ext;
					ext = strrchr(route, '.');
            
					if (ext == NULL) {
						bad_request(client, "There's no file extension");
					} else if (strnicmp(ext, ".htm", 4) == 0) {
						serve_file(client, route, "text/html");
					} else if (strnicmp(ext, ".css\0", 5) == 0) {
						serve_file(client, route, "text/css");
					} else if (strnicmp(ext, ".js\0", 4) == 0) {
						serve_file(client, route, "text/javascript");
					} else if (strnicmp(ext, ".jpg\0", 5) == 0) {
						serve_file(client, route, "image/jpeg");
					} else if (strnicmp(ext, ".jpeg\0", 6) == 0) {
						serve_file(client, route, "image/jpeg");
					} else if (strnicmp(ext, ".png\0", 5) == 0) {
						serve_file(client, route, "image/png");
					} else if (strnicmp(ext, ".gif\0", 5) == 0) {
						serve_file(client, route, "image/gif");
					} else if (strnicmp(ext, ".bmp\0", 5) == 0) {
						serve_file(client, route, "image/bmp");
					} else if (strnicmp(ext, ".ico\0", 5) == 0) {
						serve_file(client, route, "image/x-icon");
					} else if (strnicmp(ext, ".woff\0", 6) == 0) {
						serve_file(client, route, "application/x-font-woff");
					} else if (strnicmp(ext, ".txt\0", 5) == 0) {
						serve_file(client, route, "text/plain");
					}
					break;
				case FD_CLOSE:
					client = get_client(wParam);
					httpclients.remove(client);
					free(client);
					EditPrintf(hwndEdit, TEXT("=== Close one client(%d), List size:%d ===\r\n"), wParam, httpclients.size());
					break;
			};
			break;
		case WM_SOCKET_RAS:
			switch( WSAGETSELECTEVENT(lParam) )
			{
				case FD_CONNECT:
					break;
				case FD_READ:
					
					break;
				case FD_WRITE:
					break;
			}
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

HTTPClient* get_client(SOCKET ssock) {
	for (std::list<HTTPClient*>::const_iterator iterator = httpclients.begin(); iterator != httpclients.end(); ++iterator)
		if (ssock == (*iterator)->ssock)
			return *iterator;
	return NULL;
}

void bad_request(HTTPClient *client, char *msg) {
	char *bad = "HTTP/1.1 400 BAD REQUEST\nContent-Type: text/html\n\n<h1>Bad Request</h1>";
	send(client->ssock, bad, sizeof(bad) - 1, 0);

    if (msg != NULL) {
		send(client->ssock, msg, strlen(msg), 0);
	}
	client->status = HTTP_DONE;
	closesocket(client->ssock);
}

void home_page(HTTPClient *client) {
    char *home = "HTTP/1.1 200 OK\nContent-Type: text/html\n\n<h1>Home Page</h1>";
	send(client->ssock, home, sizeof(home) - 1, 0);
	client->status = HTTP_DONE;
	closesocket(client->ssock);
}

void not_found(HTTPClient *client) {
	char *not = "HTTP/1.1 404 NOT FOUND\nContent-Type: text/html\n\n<h1>Not Found</h1>";
	send(client->ssock, not, sizeof(not) - 1, 0);
	client->status = HTTP_DONE;
	closesocket(client->ssock);
}

void serve_file(HTTPClient *client, char *filename, char *content_type) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
		not_found(client);
		return;
	}

	char buf[BUF_SIZE];
	memset(buf, 0, BUF_SIZE);
	int n = sprintf(buf, "HTTP/1.1 200 OK\nContent-Type: %s\n\n", content_type);
	send(client->ssock, buf, n, 0);
	rewind(file);
	do {
		n = fread(buf, 1, BUF_SIZE, file);
		send(client->ssock, buf, n, 0);
	} while(!feof(file));
    fclose(file);
	client->status = HTTP_DONE;
	closesocket(client->ssock);
}


Client** parse_query_string(char *querystring) {
    char* item = strtok(querystring, "&");
    
    Client* *clients = (Client**)malloc(sizeof(void*) * MAX_CLIENT);
    memset(clients, 0, sizeof(void*) * MAX_CLIENT);

	if (item == NULL)
        return NULL;
    
    while (item != NULL) {
        char* ptr;
        char *key = strtok_s(item, "=", &ptr);

        if (key == NULL || strlen(key) != 2)
            break;
        
        char *val = strtok_s(NULL, "", &ptr);
        
        if (val == NULL)
            break;
        
        int clientid = atoi(key + 1);
        
        if (clientid == 0 || clientid > 5)
            break;
        
        int i = clientid - 1;
        if (clients[i] == NULL) {
            clients[i] = (Client*)malloc(sizeof(Client));
            memset(clients[i], 0, sizeof(Client));
            clients[i]->index = i;
            clients[i]->dying = FALSE;
        }
        switch (*key) {
            case 'h':
                clients[i]->host = strdup(val);
                break;
            case 'p':
                clients[i]->port = atoi(val);
                break;
            case 'f':
                clients[i]->batch = fopen(val, "r");
                break;
            default:
                break;
        }
        item = strtok(NULL, "&");
    }
    return clients;
}
