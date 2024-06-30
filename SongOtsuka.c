/*
タイマーイベントによる検知起動とSongらの手法停止
保持範囲外のCANについての処理は無し
対象CAN以外はMUSK
*/

#define _GNU_SOURCE  // GNU拡張機能を有効にするために必要

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/sockios.h>
#include <linux/can/raw.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/timerfd.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/uio.h>  // mmsghdr と recvmmsg のために必要

#define TARGET_CAN_ID 47  // 受信したい特定のCAN IDに置き換える
#define Reception_Interval 20000  //受信間隔
#define Latency_time 2000  //受信最大遅延
#define MAX_FRAMES 2  // 一度に受信する最大フレーム数

struct cyc_detect {
    int true_detect;
    int false_detect;
    int detect_frame_sum;
};

void time_check(struct cyc_detect *cyc_detect, int *O_count, int *O_A_count, bool *song_flag, FILE *file) {
    struct timeval current_time;

    gettimeofday(&current_time, NULL);

    if(2 <= *O_count + *O_A_count) {            
        if(1 <= *O_A_count) {
            cyc_detect->true_detect++;
            //printf("%ld.%06ld に攻撃検知\n", current_time.tv_sec, current_time.tv_usec);
            fprintf(file, "%ld.%06ld に攻撃検知\n", current_time.tv_sec, current_time.tv_usec);
        } else {
            cyc_detect->false_detect++;
            //printf("\x1b[31m%ld.%06ld に攻撃誤検知\x1b[0m\n", current_time.tv_sec, current_time.tv_usec);
            fprintf(file, "%ld.%06ld に攻撃誤検知\n", current_time.tv_sec, current_time.tv_usec);
        }
    }

    cyc_detect->detect_frame_sum = cyc_detect->detect_frame_sum + *O_A_count;

    *O_count = 0; // 受信回数初期化
    *O_A_count = 0;
    *song_flag = true; // Songらの手法を実行できるようにする
}

void time_check2(bool *song_flag) {
    *song_flag = false;
}

int main(void) {
    int s;  //socket
    int ret;
    fd_set rdfs;  //ファイルディスクリプタ

    //総受信回数と攻撃フレーム数
    int sum = 0;
    int A_sum = 0;

    //保持期間内の受信回数
    int O_count = 0;
    int O_A_count = 0;
    int S_count = 0;
    int S_A_count = 0;

    //最後の受信時間
    long int last_reception_time;

    struct sockaddr_can addr;  //CANインタフェースとソケットのbindに使用
    struct ifreq ifr;  //CANインタフェース
    struct can_frame frame;  //受信したCANの保持
    struct cyc_detect cyc_detect = {0,0,0};

    struct timeval tv, current_tv;

    bool initial_received = true;  //初期受信フラグ
    bool song_flag = true;  //songらの手法を実行するかどうかのフラグ

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
    FILE *file = fopen("output.txt", "w");
    if (file == NULL) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    // CANフィルタの設定
    struct can_filter rfilter[1];
    rfilter[0].can_id   = TARGET_CAN_ID;
    rfilter[0].can_mask = CAN_SFF_MASK;
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter));

    /* 検知タイマーの作成 */
    int timer_fd;
    struct itimerspec timer_spec;
    /* 初回のタイマーイベントまでの時間 */
    timer_spec.it_value.tv_sec = 0;
    timer_spec.it_value.tv_nsec = ((Reception_Interval + Latency_time) * 1000) % 1000000000;
    // ２回目以降のタイマーイベントを設定
    timer_spec.it_interval.tv_sec = 0; /* 以降のタイマーイベントの間隔 */
    timer_spec.it_interval.tv_nsec = (Reception_Interval * 1000) % 1000000000;
    timer_fd = timerfd_create(CLOCK_REALTIME, 0);
    if (timer_fd == -1) {
        perror("timerfd_create");
        return EXIT_FAILURE;
    }

    /* Songらの手法停止タイマーの作成 */
    int timer_fd2;
    struct itimerspec timer_spec2;
    /* 初回のタイマーイベントまでの時間 */
    timer_spec2.it_value.tv_sec = 0;
    timer_spec2.it_value.tv_nsec = ((Reception_Interval/2) * 1000) % 1000000000;
    // ２回目以降のタイマーイベントを設定
    timer_spec2.it_interval.tv_sec = 0; /* 以降のタイマーイベントの間隔 */
    timer_spec2.it_interval.tv_nsec = (Reception_Interval * 1000) % 1000000000;
    timer_fd2 = timerfd_create(CLOCK_REALTIME, 0);
    if (timer_fd2 == -1) {
        perror("timerfd_create");
        return EXIT_FAILURE;
    }

    //最大のファイルディスクリプタを設定
    int max_fd = (s > timer_fd ? s : timer_fd);
    max_fd = (max_fd > timer_fd2 ? max_fd : timer_fd2) + 1;

    // 受信ループ
    while (1) {
        ssize_t nframes;

        FD_ZERO(&rdfs);
        FD_SET(s, &rdfs);
        FD_SET(timer_fd, &rdfs);
        FD_SET(timer_fd2, &rdfs);

        //受信 or タイマイベントの発生まで待ち
        ret = select(max_fd, &rdfs, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select");
            return EXIT_FAILURE;
        }

        // タイマーイベント１（Otsuka判定、Song手法の有効化、カウントリセット）
        if(FD_ISSET(timer_fd, &rdfs)) {
            uint64_t exp;
            ssize_t n = read(timer_fd, &exp, sizeof(uint64_t));
            if (n != sizeof(uint64_t)) {
                perror("read timer_fd");
                return EXIT_FAILURE;
            }
            //デバッグ
            //printf("time_check起動\n");
            //以下の関数で、保持時間の更新、攻撃の有無を判定する
            time_check(&cyc_detect, &O_count, &O_A_count, &song_flag, file);
        }

        
        // タイマーイベント２（Song手法の無効化）
        if(FD_ISSET(timer_fd2, &rdfs)) {
            uint64_t exp;
            ssize_t n2 = read(timer_fd2, &exp, sizeof(uint64_t));
            if (n2 != sizeof(uint64_t)) {
                perror("read timer_fd");
                return EXIT_FAILURE;
            }
            //デバッグ
            //printf("time_check2起動\n");
            //以下の関数で、Songらの手法を停止
            time_check2(&song_flag);
        }

        if(FD_ISSET(s, &rdfs)) {
            // CANフレームを受信
            nframes = recv(s, &frame, sizeof(struct can_frame), 0);
            if (nframes < 0) {
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
                
                // 最初の受信フラグ及びタイマーのセット
                if (initial_received) {
                    timerfd_settime(timer_fd, 0, &timer_spec, NULL);
                    timerfd_settime(timer_fd2, 0, &timer_spec2, NULL);
                }

                // 受信回数をカウント
                sum++;

                //受信時間の表示
                //printf("%ld.%06ld にCAN ID %d を受信: Payload: ", tv.tv_sec, tv.tv_usec, frame.can_id);
                fprintf(file, "%ld.%06ld にCAN ID %d を受信: Payload: ", tv.tv_sec, tv.tv_usec, frame.can_id);
                for (int i = 0; i < frame.can_dlc; i++) {
                //printf("%02X ", frame.data[i]);
                fprintf(file, "%02X ", frame.data[i]);
                }
                //printf("\n");
                fprintf(file, "\n");
                
                if (!initial_received) {
                    bool all_ff = true;
                    for (int i = 0; i < frame.can_dlc; i++) {
                        if (frame.data[i] != 0xFF)
                        {
                            all_ff = false;
                        }
                    }

                    // 保持時間までの受信回数をカウント（内容別）
                    if (all_ff) {
                        O_A_count++;
                        A_sum++;
                    } else {
                        O_count++;
                    }

                    // song_flagがtrueの時、Songらの手法で検出
                    if(song_flag) {    
                        if (tv.tv_sec * 1000000 + tv.tv_usec - last_reception_time <= (Reception_Interval / 2)) {
                            gettimeofday(&current_tv, NULL);
                            // 検知フレームが正しいかのカウント
                            if (all_ff) {
                                S_A_count++;
                                //printf("%ld.%06ld の時間でCAN ID %d の攻撃を検知\n", tv.tv_sec, tv.tv_usec, frame.can_id);
                                fprintf(file, "%ld.%06ld の時間でCAN ID %d の攻撃を検知\n", current_tv.tv_sec, current_tv.tv_usec, frame.can_id);
                            } else {
                                S_count++;
                                //printf("\x1b[31m%ld.%06ld の時間でCAN ID %d の攻撃を誤検知\x1b[0m\n", tv.tv_sec, tv.tv_usec, frame.can_id);
                                fprintf(file, "%ld.%06ld の時間でCAN ID %d の攻撃を誤検知\n", current_tv.tv_sec, current_tv.tv_usec, frame.can_id);
                            }
                        }
                    }
                }

                initial_received = false;
                //最後の受信時間を更新
                last_reception_time = tv.tv_sec * 1000000 + tv.tv_usec;
            }
        }

        //500回受信で終了
        if (sum == 1000) {
            break;
        }
    }
    printf("総受信：%d 攻撃フレーム数：%d Song検知数(正しい：%d 間違い：%d) Otsuka検知数(正しい：%d 間違い: %d 総攻撃フレーム：%d)\n", sum, A_sum, S_A_count, S_count, cyc_detect.true_detect, cyc_detect.false_detect, cyc_detect.detect_frame_sum);
    fprintf(file, "総受信：%d 攻撃フレーム数：%d Song検知数(正しい：%d 間違い：%d) Otsuka検知数(正しい：%d 間違い: %d 総攻撃フレーム：%d)\n", sum, A_sum, S_A_count, S_count, cyc_detect.true_detect, cyc_detect.false_detect, cyc_detect.detect_frame_sum);
    close(s);
    close(timer_fd);
    close(timer_fd2);
    fclose(file);
    return EXIT_SUCCESS;
}