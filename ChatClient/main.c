//
//  main.c
//  ChatClient
//
//  Created by 桜井雄介 on 2013/07/10.
//  Copyright (c) 2013年 Yusuke Sakurai. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#define SEND_WIN_WIDTH 60
#define SEND_WIN_HEIGHT 1
#define TO_WIN_WIDTH 60
#define TO_WIN_HEIGHT 1
#define RECV_WIN_WIDTH 60
#define RECV_WIN_HEIGHT 13

#define BUF_LEN 512

/* プロトコルのコマンド */
typedef enum{
    host_cmd_unknown = 0,
    host_cmd_register,      // 名前登録
    host_cmd_send,          // メッセージ送信
    host_cmd_lists          // 接続中のクライアント一覧
}host_cmd_t;

typedef enum{
    client_cmd_recieve,     // メッセージ受信
    client_cmd_change_user, // ユーザ数の変化
    client_cmd_errror       // エラー
}client_cmd_t;


static char to_buf[BUF_LEN];    // 宛先用バッファ
static char send_buf[BUF_LEN];  // 送信用バッファ
static char recv_buf[BUF_LEN];  // 受信用バッファ
static WINDOW *to_wind, *send_wind, *recv_wind; // windows
static WINDOW *to_frame, *send_frame, *recv_frame; // frames
static WINDOW *active_wind;

void init_window(void)
{

    // windowを初期化
    initscr();

    // 受信用windowを作る
    recv_frame = newwin(RECV_WIN_HEIGHT + 2, RECV_WIN_WIDTH + 2, 0, 0);
    recv_wind = newwin(RECV_WIN_HEIGHT, RECV_WIN_WIDTH, 1, 1);
    box(recv_frame, ';', '-');
    scrollok(recv_wind, TRUE);
    wmove(recv_wind, 0, 0);

    // 宛先用windowを作る
    to_frame = newwin(TO_WIN_HEIGHT + 2, TO_WIN_WIDTH + 2, RECV_WIN_HEIGHT + 3, 0);
    to_wind = newwin(TO_WIN_HEIGHT, TO_WIN_WIDTH , RECV_WIN_HEIGHT + 4, 1);
    box(to_frame, ';', '-');
    scrollok(to_wind, TRUE);
    wmove(to_wind, 0, 0);

    // 送信用windowを作る
    send_frame = newwin(SEND_WIN_HEIGHT + 2, SEND_WIN_WIDTH + 2, RECV_WIN_HEIGHT + TO_WIN_HEIGHT + 6 , 0);
    send_wind = newwin(SEND_WIN_HEIGHT, SEND_WIN_WIDTH, RECV_WIN_HEIGHT + TO_WIN_HEIGHT + 7, 1);
    box(send_frame, ';', '-');
    scrollok(send_wind, TRUE);
    wmove(send_wind, 0, 0);

    active_wind = to_wind;

    cbreak();
//    noecho();

    wrefresh(recv_frame);
    wrefresh(recv_wind);
    wrefresh(send_frame);
    wrefresh(send_wind);
    wrefresh(to_frame);
    wrefresh(to_wind);

}

int connect_to_server(char *host, char *port)
{
    // 結果のaiを指定する構造体
    struct addrinfo hints;
    // getaddrinfoの最初の結果
    struct addrinfo *ai_head;
    // ソケット
    int s;
    // エラーの場所
    const char *cause = NULL;
    // バイトメモリを初期化
    memset(&hints, 0, sizeof(hints));
    // 希望アドレスファミリーを指定
    hints.ai_family = PF_UNSPEC;
    // 希望ソケット型を指定
    hints.ai_socktype = SOCK_STREAM;

    // DSNからIPアドレス情報を取得
    int gai = getaddrinfo(host, port, &hints, &ai_head);
    if (gai < 0) {
        waddstr(recv_wind, gai_strerror(gai));
        // こないはず
        exit(EXIT_FAILURE);
    }
    // ソケットのファイルディスクリプタを初期化
    s = -1;
    // インクリメント用のポインタを初期化
    struct addrinfo *ai;
    ai = ai_head;
    do {
        // ソケットを作成
        s = socket(ai->ai_family, ai->ai_socktype,ai->ai_protocol);
        // 作成に失敗したら次のアドレスへ
        if (s < 0) {
            cause = "socket";
            continue;
        }
        // ソケットに接続
        if (connect(s, ai->ai_addr, ai->ai_addrlen) < 0) {
            // 失敗したら閉じて次へ
            cause = "connect";
            close(s);
            s = -1;
            continue;
        }
    } while ((ai = ai->ai_next));
    return s;
}

/*
 * @params argv
 *
 */
int main(int argc, char* argv[]){
    // ソケット
    int sock;
    // selectするfd_set
    fd_set read_fds,read_fds0;

    // windowを初期化
    init_window();

    // 接続開始
    char *host, *port, *name;
#ifdef DEBUG
    host = "133.27.78.162";
    port = "3901";
    name = "keroxp";
#else
    if(argc < 4){
        waddstr(recv_wind, "引数が足りない\n");
        return (-1);
    }
    host = argv[1];
    port = argv[2];
    name = argv[3];
#endif
    sock = connect_to_server(host, port);
    if (sock < 0) {
        // 来ないはず
        waddstr(recv_wind, "ソケットの作成に失敗しました\n");
        exit(EXIT_FAILURE);
    }
    // 接続が確立されたら名前を登録する
    char rgst_buf[BUF_LEN];
    sprintf(rgst_buf, "%zi\n",strlen(name));
    sprintf(rgst_buf + strlen(rgst_buf), "%i\n",host_cmd_register);
    sprintf(rgst_buf + strlen(rgst_buf), "%s\n", name);

    if (write(sock, rgst_buf, strlen(rgst_buf)) > 1) {
        // registerd
        waddstr(recv_wind, "You Are Registerd.\n");
    }else{
        // not registerd
        waddstr(recv_wind, "!! You Are Not Registered !!\n");
        exit(EXIT_FAILURE);
    }

    int c;      // 標準入力からの文字
    int send_len, to_len = 0;    // 入力文字の長さ
    int y, x;   // 座標

    // ノンブロックで読み込み
    FD_ZERO(&read_fds);
    FD_SET(0, &read_fds);
    FD_SET(sock, &read_fds);

    while (1) {
        read_fds0 = read_fds;
        if (select(sock + 1, &read_fds0, NULL, NULL, NULL) < 1) {
            perror("select");
            exit(EXIT_FAILURE);
        }
        int *len = (active_wind == to_wind) ? &to_len : &send_len;
        char *buf = (active_wind == to_wind) ? to_buf : send_buf;
        // 標準入力からか
        if (FD_ISSET(0, &read_fds0)) {
            c = getchar();
            if (c == '\b' || c == 0x10 || c == 0x7f) {
                // 削除
                if (*len == 0) {
                    continue;
                }
                (*len)--;
                getyx(active_wind, y, x);
                wmove(active_wind, y, x - 1);
                waddch(active_wind, ' ');
                wmove(active_wind, y, x - 1);
            }else if (c == '\n'){
                // 送信
                if (active_wind == to_wind) {
                    // 次の欄へ
                    wmove(send_wind, 0, send_len);
                    active_wind = send_wind;
                }else{
                    // 送信
                    wclear(send_wind);

//                    write(sock, send_buf, len);
                    to_len = 0;
                    send_len = 0;
                }
            }else if (c == '\t'){
                // カーソル移動
                if (active_wind == to_wind) {
                    wmove(send_wind, 0, send_len);
                    active_wind = send_wind;
                }else{
                    wmove(to_wind, 0, to_len);
                    active_wind = to_wind;
                }
            }else{
                // 追加
                buf[(*len)++] = c;
                waddch(active_wind, c);
            }
            wrefresh(active_wind);
        }
        // ソケットからか
        ssize_t recv_len = 0;
        if (FD_ISSET(sock, &read_fds0)) {
            recv_len = read(sock, recv_buf, BUF_LEN);
            // パーシング
            int i;
            for (i = 0; i < recv_len; i++) {
                waddstr(recv_wind, recv_buf);
            }
            wrefresh(recv_wind);
            wmove(active_wind, 0, *len);
            wrefresh(active_wind);
        }
    }
    return 0;
}