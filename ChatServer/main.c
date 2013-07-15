//
//  main.c
//  ChatServer
//
//  Created by 桜井雄介 on 2013/07/08.
//  Copyright (c) 2013年 Yusuke Sakurai. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>

/* 接続しているクライアントのリスト */
struct client {
    int fd;                 // ファイルディスクリプタへのポインタ
    char name[8];           // スクリーンネーム
    struct client *next;    // リストの次
};

/* プロトコルのコマンド */
typedef enum{
    host_cmd_unknown = 0,
    host_cmd_register,       // 接続
    host_cmd_send,          // メッセージ送信
    host_cmd_lists          // 接続中のクライアント
}host_cmd_t;

typedef enum{
    client_cmd_recieve,     // メッセージ受信
    client_cmd_errror       // エラー
}client_cmd_t;

#define BUF_LEN 512

#pragma mark - Variables

// テイルリストの先頭
static struct client *clientHead = NULL;
// テイルリストの最後
static struct client *clientTail = NULL;
// ソケットアドレス
static struct sockaddr *sa;
// リスニングソケット
static fd_set listening_fds;
// 監視対象のソケット
static fd_set watching_fds;
// fdから読み込んだサイズ
static size_t left_read_sizes[FD_SETSIZE];
// fdに書き込んだサイズ
static size_t left_write_sizes[FD_SETSIZE];


#pragma mark - Functions

int _test (void);

ssize_t _read(int fd, void *buf, size_t len);
ssize_t _write(int fd, void *buf, size_t len);

int listen_to_clients(const char *port);

int handle_listening_socket(int fd, int max_fd);
int handle_client_socket(int fd);

int parse_message(const char *msg);
int handle_register(int fd, const char *name);
int handle_send(int from, char *tos, char *msg);

void add_client(int fd, const char *name);
void remove_client(int fd);

char * get_client_name(int fd);
const char * get_client_command_name(client_cmd_t cmd);
const char * get_host_command_name(host_cmd_t cmd);

int get_client_fd(const char *name);
int is_client(int fd);

#pragma mark - Main

int main (int argc, char *argv[])
{
//    return _test();
    
    // fdのビット幅
    int max_fd;
    // 受付開始
    max_fd = listen_to_clients("3901");
    printf("最大fd : %i\n",max_fd);
    printf("リスニングを開始しました...\n");
    
    // メインループ
    while(1){
        // fd_setをoriginalからコピー
        fd_set fds;
        memcpy(&fds, &watching_fds, sizeof(fd_set));
        // selectでブロック
        if(select(max_fd + 1,&fds,NULL,NULL,NULL) <= 0) {
            perror("select");
            exit(EXIT_FAILURE);
        }
        // どれが受け入れ可能になったかを順に見ていく
        int fd;
        for (fd = 0 ; fd < FD_SETSIZE ; fd++) {
            if(FD_ISSET(fd, &fds)){
                if (FD_ISSET(fd, &listening_fds)) {
                    max_fd = handle_listening_socket(fd, max_fd);
                }else{
                    // 接続済みソケットから来たら
                    printf("fd#%i のクライアントソケットが読み込み可能になりました\n",fd);
                    // 読み込む
                    size_t done = 0;
                    char msg_buf[BUF_LEN];
                    done = read(fd, msg_buf, BUF_LEN);
                    if (done < 1) {
                        // 切断されたっぽかったら監視対象から外してclose
                        printf("fd#%i が切断されました\n",fd);
                        FD_CLR(fd, &watching_fds);
                        remove_client(fd);
                        close(fd);
                        break;
                    }
                    size_t len = atoi(strtok(msg_buf, "\n"));
                    printf("メッセージの長さは%zi、",len);
                    host_cmd_t cmd = atoi(strtok(NULL, "\n"));
                    printf("コマンドは<%s>です\n",get_host_command_name(cmd));
                    // コマンドによって分岐
                    switch (cmd) {
                            // 登録
                        case host_cmd_register: {
                            // 名前を取得
                            char *name;
                            name = strtok(NULL, "\n");
                            // リストにレジスト
                            add_client(fd, name);
                            printf("fd#%iを<%s>さんとして登録しました\n",fd,name);
                        }
                            break;
                            // 送信
                        case host_cmd_send: {
                            printf("fromは %s 、", get_client_name(fd));
                            // toリストを取得
                            char *tos, *msg;
                            tos = strtok(NULL, "\n");
                            printf("toは %s 、",tos);
                            msg = strtok(NULL, "\n");
                            printf("メッセージは \"%s\" です\n",msg);
                            handle_send(fd, tos, msg);
                        }
                            break;
                        default:
                            break;
                    }

                }
                break;
            }
        }
    }
    return 0;
}

int handle_send(int from, char *tos, char *msg)
{
    int to_fds[10];
    int to_len = 1;
    to_fds[0] = from;
    to_fds[1] = get_client_fd(strtok(tos, ","));
    while (1) {
        char *name = strtok(NULL, ",");
        if (name != NULL) {
            to_len++;
            to_fds[to_len] = get_client_fd(name);
        }else{
            break;
        }
    }
    int i;
    char body[BUF_LEN];
    sprintf(body, "%s:< %s",get_client_name(from),msg);
    // 書き込み
    printf("%i\n",to_len);
    for (i = 0; i < to_len + 1; i++) {
        write(to_fds[i], body, strlen(body));
    }
    return 0;
}

int listen_to_clients(const char *port)
{
    // 結果のaiを指定する構造体
    struct addrinfo hints;
    // getaddrinfoの最初の結果
    struct addrinfo *ai_head;

    // バイトメモリを初期化
    memset(&hints, 0, sizeof(hints));
    // アドレスインフォをパッシブに
    hints.ai_flags = AI_PASSIVE;
    // 希望アドレスファミリーを指定
    hints.ai_family = PF_UNSPEC;
    // 希望ソケット型を指定
    hints.ai_socktype = SOCK_STREAM;

    // DSNからIPアドレス情報を取得
    int gai = getaddrinfo(NULL, "3901", &hints, &ai_head);
    if (gai < 0) {
        gai_strerror(gai);
        exit(EXIT_FAILURE);
    }
    // メモリを埋める
    memset(&sa,0,sizeof(sa));
    // fd_setをクリア
    FD_ZERO(&watching_fds);
    FD_ZERO(&listening_fds);

    // リスニング用のソケットを作成
    struct addrinfo *res;
    res = ai_head;
    int max = 0;
    while (res != NULL) {
        // ソケットを作成
        int s = -1;
        int opt = 1;
        s = socket(res->ai_family, res->ai_socktype,res->ai_protocol);
        // 作成に失敗したら次のアドレスへ
        if (s < 0) {
            continue;
        }
        if(res->ai_family == AF_INET || res->ai_family == AF_INET6) {
			setsockopt(s,
                       SOL_SOCKET,
                       SO_REUSEADDR,
                       (const char *)&opt,
                       sizeof(opt));
		}

		if(res->ai_family == AF_INET6) {
			setsockopt(s,
                       IPPROTO_IPV6,
                       IPV6_V6ONLY,
                       (const char *)&opt,
                       sizeof(opt));
		}
        // ソケットをbind
        if (bind(s, res->ai_addr, res->ai_addrlen) < 0) {
            // 失敗したら閉じて次へ
            close(s);
            s = -1;
            continue;
        }
        // listen
        listen(s,5);
        // fd_setにソケットを追加
        FD_SET(s, &watching_fds);
        FD_SET(s, &listening_fds);
        max = (s > max) ? s : max;
        res = res->ai_next;
    }
    return max;
}


int handle_listening_socket(int fd, int max_fd)
{
    printf("fd#%i のリスニングソケットが読み込み可能になりました\n",fd);
    // リスニングから新しいソケットが来たらaccept
    socklen_t sclen = sizeof(sa);
    int ns = accept(fd, (struct sockaddr*)&sa, &sclen);
    if(ns > 0){
        // 監視対象に追加
        FD_SET(ns, &watching_fds);
        printf("fd#%i からきた socket#%i をAcceptしました\n",fd,ns);
    }else{
        printf("fd#i はacceptできませんでした\n");
    }
    return (ns > max_fd) ? ns : max_fd;
}

#pragma mark - Util

const char * get_host_command_name(host_cmd_t cmd)
{
    switch (cmd) {
        case host_cmd_register:
            return "Register";
        case host_cmd_send:
            return "Send";
        case host_cmd_unknown:
            return "Unknown";
        default:
            break;
    }
    return "Undefined";
}

const char * get_client_command_name(client_cmd_t cmd)
{
    switch (cmd) {
        case client_cmd_errror:
            return "Error";
        case client_cmd_recieve:
            return "Recieve";
        default:
            break;
    }
    return "Undefined";
}

#pragma mark - Client Table


int _test(void)
{
    
    add_client(3, "three");
    add_client(4, "four");
    assert(is_client(4));
    assert(!is_client(5));
    remove_client(3);
    assert(strcmp(get_client_name(4),"four") == 0);
    assert(get_client_fd("four") == 4);
    assert(!is_client(3));
    remove_client(4);
    remove_client(1);
    remove_client(-1);
    return 1;
}

/* クライアントをリストに追加する */
void add_client(int fd, const char *name)
{
    // 新しいクライアントを作成
    struct client *new;
    new = malloc(sizeof(new));
    new->fd = fd;
    strcpy(new->name, name);
    if (clientHead == NULL) {
        clientHead = new;
        clientTail = new;
    }else{
        clientTail->next = new;
        clientTail = new;
    }
    new->next = NULL;
}

/* クライアントをリストから削除する */
void remove_client(int fd)
{
    struct client *c;
    struct client *p;
    if (!is_client(fd)) {
        return;
    }
    c = clientHead;
    while (c != NULL) {
        if (c->fd == fd) {
            if (c == clientHead) {
                clientHead = c->next;
            }else if (c == clientTail){
                clientTail = p;
            }else{
                p->next = c->next;
            }
            free(c);            
            printf("fd#%i は削除されました\n",fd);
            return;
        }
        p = c;
        c = c->next;
    }
    printf("fd#%i の削除に失敗しました\n",fd);
}

/* ファイルディスクリプタからクライアントを特定 */
char * get_client_name(int fd)
{
    struct client *c;
    c = clientHead;
    while (c != NULL) {
        if (c->fd == fd) {
            printf("fd#%i<\"%s\">が見つかりました\n",fd,c->name);
            return c->name;
        }
        c = c->next;
    }
    printf("fd#%iは見つかりませんでした\n",fd);
    return NULL;
}
/* 名前からクライアントを特定 */
int get_client_fd(const char *name)
{
    struct client *c;
    c = clientHead;
    while (c != NULL) {
        if (strcmp(c->name, name) == 0) {
            printf("fd#%i<\"%s\">が見つかりました\n",c->fd,c->name);
            return c->fd;
        }
        c = c->next;
    }
    printf("fd<\"%s\">は見つかりませんでした\n",name);
    return -1;
}

/* クライアントはリストにあるか？ */
int is_client(int fd)
{
    struct client *c;
    c = clientHead;
    while (c != NULL) {
        if (c->fd == fd) {
            printf("fd#%iはリストにあります\n",fd);
            return 1;
        }
        c = c->next;
    }
    printf("fd#%iはリストにはありません\n",fd);
    return 0;
}

#pragma mark - IO

ssize_t _io(int rd, int fd, void *buf, size_t len)
{
    size_t left;
    left = (rd) ? left_read_sizes[fd] : left_write_sizes[fd];
    ssize_t done = 0;
    while (done < len) {
        done += (rd) ? read(fd, buf, len) : write(fd, buf, len);
        if (done < 1) {
            return done;
        }
        left -= done;
    }
    if (rd) {
        left_read_sizes[fd] = left;
    }else{
        left_write_sizes[fd] = left;
    }
    return done;
}

ssize_t _read(int fd, void *buf, size_t len)
{
    return _io(1,fd,buf,len);
}

ssize_t _write(int fd, void *buf, size_t len)
{
    return _io(0,fd,buf,len);
}