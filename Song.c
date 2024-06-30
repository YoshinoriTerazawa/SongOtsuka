#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/sockios.h>
#include <linux/can/raw.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>

#define TARGET_CAN_ID 47  // 受信したい特定のCAN IDに置き換える
#define Reception_Interval 20000  //受信間隔

int main(void) {
    int s;  //socket
    int ret;
    fd_set rdfs;  //ファイルディスクリプタ
    int count = 0;  //保持期間内の受信回数
    struct sockaddr_can addr;  //CANインタフェースとソケットのbindに使用
    struct ifreq ifr;  //CANインタフェース
    struct can_frame frame;  //受信したCANの保持
    struct timeval tv, timeout;  //受信時間、タイムアウトの時間
    bool initial_received = true;  //初期受信フラグ
    long int last_reception_time;  //前回の受信時間
    long int reception_time;  //受信時間



    // CANソケットの作成
    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // CANインタフェースへのバインド
    memset(&ifr.ifr_name, 0, sizeof(ifr.ifr_name));
    strcpy(ifr.ifr_name, "can0");
    ioctl(s, SIOCGIFINDEX, &ifr);

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return EXIT_FAILURE;
    }

    printf("Socket bound to CAN interface\n");

    // 出力ファイルをオープン
    FILE *file = fopen("Song.txt", "w");
    if (file == NULL) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    while(1) {
        ssize_t nbytes;

        //CANフレーム受信
        nbytes = recv(s, &frame, sizeof(struct can_frame), 0);
        if (nbytes < 0) {
            perror("recv");
            return EXIT_FAILURE;
        }

        // 特定のCAN IDを持つフレームのみ処理
        if (frame.can_id == TARGET_CAN_ID) {
            // 受信時の時間を取得
            if (ioctl(s, SIOCGSTAMP, &tv) == -1) {
                perror("ioctl");
                return EXIT_FAILURE;
            }

            reception_time = tv.tv_sec * 1000000 + tv.tv_usec;  //long int型に変更し、計算可能にする
            printf("%d.%06ld にCAN ID %d を受信: Payload: ", tv.tv_sec, tv.tv_usec, frame.can_id);
            fprintf(file, "%d.%06ld にCAN ID %d を受信: Payload: ", tv.tv_sec, tv.tv_usec, frame.can_id);
            for (int i = 0; i < frame.can_dlc; i++) {
                printf("%02X ", frame.data[i]);
                fprintf(file, "%02X ", frame.data[i]);
            }
            printf("\n");
            fprintf(file, "\n");

            // 最初の受信フラグ
            if(initial_received) {
                initial_received = false;
            } else
            // 2回目以降は判定を行う
            if(reception_time - last_reception_time < Reception_Interval/2) {
                printf("%d.%06ld の時間でCAN ID %d の攻撃を検知\n", tv.tv_sec, tv.tv_usec, frame.can_id);
                fprintf(file, "%d.%06ld の時間でCAN ID %d の攻撃を検知\n", tv.tv_sec, tv.tv_usec, frame.can_id);
            };

            last_reception_time = reception_time;
        }

    }
}