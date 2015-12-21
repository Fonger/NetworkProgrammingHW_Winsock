#include <windows.h>
#include <list>
#include <string.h>

using namespace std;

#include "resource.h"

#define SERVER_PORT 7890
#define BUF_SIZE 20000
#define MAX_CLIENT 5
#define MAX_LINE 10000
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
} Client;

typedef struct {
	SOCKET  ssock;
	char    buffer[BUF_SIZE];
	int     status;
} HTTPClient;


BOOL CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
int EditPrintf (HWND, TCHAR *, ...);
HTTPClient* get_http_client(SOCKET ssock);
Client* get_client(SOCKET ssock);
void bad_request(HTTPClient *client, char *msg);
void home_page(HTTPClient *client);
void not_found(HTTPClient *client);
void serve_file(HTTPClient *client, char *filename, char *content_type);
Client** parse_query_string(char *querystring);
void print_html_frame(Client** clients);
void printc(Client* client, char* content, int bold);
char *str_replace(char *orig, char *rep, char *with);


//=================================================================
//	Global Variables
//=================================================================
list<HTTPClient*> httpclients;
Client* *clients = NULL;
SOCKET  httpsock = NULL;
int     nclients = 0;

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
	static HTTPClient *httpclient;
	static Client *client;
	static int rResult;

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
					
					httpclient = (HTTPClient*)malloc(sizeof(HTTPClient));
					memset(httpclient, 0, sizeof(HTTPClient));
					httpclient->ssock = ssock;
					httpclient->status = HTTP_ACCEPT;
					httpclients.push_back(httpclient);

					EditPrintf(hwndEdit, TEXT("=== Accept one new client(%d), List size:%d ===\r\n"), ssock, httpclients.size());
					break;
				case FD_READ:
					//Write your code for read event here.
					httpclient = get_http_client(wParam);
					if (httpclient == NULL)
						break;
					if (httpclient->status != HTTP_ACCEPT)
						break;

					rResult = recv(httpclient->ssock, httpclient->buffer, BUF_SIZE, 0);
					if (rResult < 0) {
						int err = WSAGetLastError();
						if (err == WSAEWOULDBLOCK)
							break;
						EditPrintf(hwndEdit, TEXT("=== Fail to recv (%d) code: 0x%x ===\r\n"), msock, err);
						break;
					}
					httpclient->buffer[rResult] = '\0';
					OutputDebugString(httpclient->buffer);
					httpclient->status = HTTP_WRITE;
				case FD_WRITE:
					//Write your code for write event here
					httpclient = get_http_client(wParam);
					if (httpclient == NULL)
						break;
					if (httpclient->status != HTTP_WRITE)
						break;

					char *url;
					char *method;
					method = strtok(httpclient->buffer, " ");
            
					if (*method == '\0') {
						bad_request(httpclient, "No method");
						break;
					}
            
					url = strtok(NULL, " ");
            
					if (url == NULL || *url == '\0') {
						bad_request(httpclient, "No url found");
						break;
					}

					//GET /test?qq=123 HTTP/1.1
            
					char *route;
					route = strtok(url, "?");
            
					if (route == NULL || *route == '\0') {
						home_page(httpclient);
						break;
					}
            
					route++; // exclude the first '/' character
            
					if (*route == '\0') {
						home_page(httpclient);
						break;
					}
            
					char *querystring;
					querystring = strtok(NULL, "");

					if (strncmp(route, "hw3.cgi\0", 8) == 0) {
						nclients = 0;
						clients = parse_query_string(querystring);
						print_html_frame(clients);
						for (int i = 0; i < MAX_CLIENT; i++) {
							if (clients[i] == NULL)
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

							struct hostent *he = gethostbyname(clients[i]->host);

							//fill the address info about server
							sa.sin_family		= AF_INET;
							sa.sin_port			= htons(clients[i]->port);
							sa.sin_addr	        = *((struct in_addr *)he->h_addr);

							clients[i]->sockfd = rsock;
							clients[i]->status = F_CONNECTING;
							
							err = connect(rsock, (LPSOCKADDR)&sa, sizeof(struct sockaddr));
							if( err == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
								EditPrintf(hwndEdit, TEXT("=== Error: connect error in ras ===\r\n"));
								WSACleanup();
								return FALSE;
							}
						}
						break;
					}

					char *ext;
					ext = strrchr(route, '.');
            
					if (ext == NULL) {
						bad_request(httpclient, "There's no file extension");
					} else if (strnicmp(ext, ".htm", 4) == 0) {
						serve_file(httpclient, route, "text/html");
					} else if (strnicmp(ext, ".css\0", 5) == 0) {
						serve_file(httpclient, route, "text/css");
					} else if (strnicmp(ext, ".js\0", 4) == 0) {
						serve_file(httpclient, route, "text/javascript");
					} else if (strnicmp(ext, ".jpg\0", 5) == 0) {
						serve_file(httpclient, route, "image/jpeg");
					} else if (strnicmp(ext, ".jpeg\0", 6) == 0) {
						serve_file(httpclient, route, "image/jpeg");
					} else if (strnicmp(ext, ".png\0", 5) == 0) {
						serve_file(httpclient, route, "image/png");
					} else if (strnicmp(ext, ".gif\0", 5) == 0) {
						serve_file(httpclient, route, "image/gif");
					} else if (strnicmp(ext, ".bmp\0", 5) == 0) {
						serve_file(httpclient, route, "image/bmp");
					} else if (strnicmp(ext, ".ico\0", 5) == 0) {
						serve_file(httpclient, route, "image/x-icon");
					} else if (strnicmp(ext, ".woff\0", 6) == 0) {
						serve_file(httpclient, route, "application/x-font-woff");
					} else if (strnicmp(ext, ".txt\0", 5) == 0) {
						serve_file(httpclient, route, "text/plain");
					}
					break;
				case FD_CLOSE:
					httpclient = get_http_client(wParam);
					httpclients.remove(httpclient);
					free(httpclient);
					EditPrintf(hwndEdit, TEXT("=== Close one client(%d), List size:%d ===\r\n"), wParam, httpclients.size());
					break;
			};
			break;
		case WM_SOCKET_RAS:
			switch (WSAGETSELECTEVENT(lParam))
			{
				case FD_CONNECT:
					client = get_client(wParam);
					if (client == NULL)
						break;
					if (client->status != F_CONNECTING)
						break;

					client->status = F_READING;
					nclients++;

					break;
				case FD_READ:
					client = get_client(wParam);
					if (client == NULL)
						break;
					if (client->status != FD_READ)
						break;

					char rasbuf[BUF_SIZE];

					rResult = recv(client->sockfd, rasbuf, BUF_SIZE, 0);
					if (rResult < 0) {
						int err = WSAGetLastError();
						if (err == WSAEWOULDBLOCK)
							break;
						EditPrintf(hwndEdit, TEXT("=== Fail to recv (%d) code: 0x%x ===\r\n"), msock, err);
						break;
					}
					rasbuf[rResult] = '\0';
					OutputDebugString(rasbuf);

					for(int j = 0; j < rResult - 1; j++) { //possible problem
                        if (client->dying) {
							if (client->status != F_DONE) {
								client->status = F_DONE;
                                nclients--;
                            }
                        }
                        if (rasbuf[j] == '%' && rasbuf[j + 1] == ' ') {
							client->status = F_WRITING;
                            break;
                        }
                    }
					// break; fall through
				case FD_WRITE:
					client = get_client(wParam);
					if (client == NULL)
						break;
					if (client->status != FD_WRITE)
						break;

					char* cmd;

					if (client->lastcmd != NULL)
						cmd = client->lastcmd;
					else
						cmd = (char*)malloc(MAX_LINE);

					if (fgets(cmd, MAX_LINE, client->batch) != NULL) {
						if (send(client->sockfd, cmd, strlen(cmd), 0) < 0) {
							if (WSAGetLastError() == WSAEWOULDBLOCK) {
								client->lastcmd = cmd;
								break;
							}
							else
								EditPrintf(hwndEdit, TEXT("Write cmd failed"));
						}

						printc(client, cmd, TRUE);

						if (strncmp(cmd, "exit", 4) == 0)
							client->dying = TRUE;

						free(cmd);
						client->lastcmd = NULL;

						client->status = F_READING;
					}
					else if (client->status != F_DONE) {
						client->status = F_DONE;
						nclients--;
					}

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

HTTPClient* get_http_client(SOCKET ssock) {
	for (std::list<HTTPClient*>::const_iterator iterator = httpclients.begin(); iterator != httpclients.end(); ++iterator)
		if (ssock == (*iterator)->ssock)
			return *iterator;
	return NULL;
}

Client* get_client(SOCKET ssock) {
	for (int i = 0; i < MAX_CLIENT; i++) {
		if (clients[i] == NULL)
			continue;
		if (ssock == clients[i]->sockfd)
			return clients[i];
	}
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


void printh(const char *format, ...) {
	va_list args;
	char *msg = (char*)malloc(MAX_LINE);
	va_start(args, format);
	int n = vsnprintf(msg, MAX_LINE, format, args);
	va_end(args);
	size_t result = send(httpsock, msg, n, 0);
	if (result < 0) {
		if (WSAGetLastError() != WSAEWOULDBLOCK) {
			OutputDebugString("FAILURE");
		}
	}
	free(msg);
}
void print_html_frame(Client* *clients) {
	printh("HTTP1.1 200 OK\nContent-Type: text/html\n\n<html>\n");
	printh("<head>\n");
	printh("	<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />\n");
	printh("	<title>Network Programming Homework 3</title>\n");
	printh("</head>\n");
	printh("<body bgcolor=#336699>\n");
	printh("	<font face=\"Courier New\" size=2 color=#FFFF99>\n");
	printh("		<table width=\"800\" border=\"1\">\n");
	printh("			<tr>\n");

	for (int i = 0; i < MAX_CLIENT; i++) {
		Client* client = clients[i];
		if (client == NULL)
			continue;
		printh("		  <td>%s</td>\n", client->host);
	}
	printh("			</tr>\n");
	printh("			<tr>\n");

	for (int j = 0; j < MAX_CLIENT; j++) {
		Client* client = clients[j];
		if (client == NULL)
			continue;
		printh("			<td valign=\"top\" id=\"m%d\">\n", client->index);
		printh("            </td>\n");
	}
	printh("			</tr>\n");
	printh("		</table>\n");
	printh("	</font>\n");
	printh("</body>\n");
	printh("</html>\n");
}

void printc(Client* client, char* content, int bold) {
	char *first, *second, *third, *forth, *final;
	first = str_replace(content, "<", "&lt;");
	second = str_replace(first, ">", "&gt;");
	free(first);
	third = str_replace(second, "\"", "\\\"");
	free(second);
	forth = str_replace(third, "\r\n", "<br>");
	free(third);
	final = str_replace(forth, "\n", "<br>");

	if (bold)
		printh("<script>document.all['m%d'].innerHTML+=\"<b>%s</b>\";</script>\n", client->index, final);
	else
		printh("<script>document.all['m%d'].innerHTML+=\"%s\";</script>\n", client->index, final);
	free(final);
}

// You must free the result if result is non-NULL.
char *str_replace(char *orig, char *rep, char *with) {
	char *result; // the return string
	char *ins;    // the next insert point
	char *tmp;    // varies
	size_t len_rep;  // length of rep
	size_t len_with; // length of with
	size_t len_front; // distance between rep and end of last rep
	int count;    // number of replacements

	if (!orig)
		return NULL;
	if (!rep)
		rep = "";
	len_rep = strlen(rep);
	if (!with)
		with = "";
	len_with = strlen(with);

	ins = orig;
	for (count = 0; (tmp = strstr(ins, rep)); ++count) {
		ins = tmp + len_rep;
	}

	// first time through the loop, all the variable are set correctly
	// from here on,
	//    tmp points to the end of the result string
	//    ins points to the next occurrence of rep in orig
	//    orig points to the remainder of orig after "end of rep"
	tmp = result = (char*)malloc(strlen(orig) + (len_with - len_rep) * count + 1);

	if (!result)
		return NULL;

	while (count--) {
		ins = strstr(orig, rep);
		len_front = ins - orig;
		tmp = strncpy(tmp, orig, len_front) + len_front;
		tmp = strcpy(tmp, with) + len_with;
		orig += len_front + len_rep; // move to next "end of rep"
	}
	strcpy(tmp, orig);
	return result;
}