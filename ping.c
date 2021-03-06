#include	<arpa/inet.h>
#include	<errno.h>
#include	<netdb.h>
#include	<netinet/ip_icmp.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<sys/time.h>
#include	<time.h>
#include	<unistd.h>
#include	<poll.h>

#define	BUFSIZE		1500
#define	ECHO_HDR_SIZE	8

int calc_checksum(u_short *ptr, int nbytes)
{
  long sum;
  u_short oddbyte;

  sum = 0;
  while (nbytes > 1) {
    sum += *ptr++;
    nbytes -= 2;
  }

  if (nbytes == 1) {
    oddbyte = 0;
    *((u_char *)&oddbyte)=*(u_char *)ptr;
    sum+=oddbyte;
  }

  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += (sum >> 16);
  return ~sum;
}

// ICMPエコーリクエストを作成し送信する
int send_ping(int soc, char *name, int len, unsigned short sqc, struct timeval *sendtime)
{
  struct hostent *host;
  // 接続先のIPアドレスやポート番号等を保持する構造体
  struct sockaddr_in *sinp;
  struct sockaddr sa;
  // ICMPヘッダ
  struct icmphdr *icp;
  unsigned char *ptr;
  int psize;
  int n;
  // 送信するデータを格納するバッファ領域
  char sbuff[BUFSIZE];

  sinp = (struct sockaddr_in *)&sa;
  sinp -> sin_family = AF_INET;

  if ((sinp->sin_addr.s_addr = inet_addr(name)) == INADDR_NONE) {
    // 接続先のホスト名nameに対応するネットワークアドレス等を含む構造体hostentを返す
    host = gethostbyname(name);
    if (host == NULL) {
      return -100;
    }
    sinp->sin_family = host->h_addrtype;
    memcpy(&(sinp->sin_addr),host->h_addr,host->h_length);
  }

  // sendtimeに現在の時刻を格納する
  gettimeofday(sendtime, NULL);

  // 送信データの作成
  // メモリ領域を確保する(0で埋める)
  memset(sbuff, 0, BUFSIZE);
  // バッファ領域をicmphdrのポインタに変換?
  icp = (struct icmphdr *)sbuff;
  // 今回はECHO Requestを送信するのでtypeはICMP_ECHO
  icp->type = ICMP_ECHO;
  // 常に0
  icp->code = 0;
  // htonsはunsigned shortをTCP/IPで使用されるNetwork byte orderに変換する
  // idにはpidを入れる
  icp->un.echo.id = htons((unsigned short) getpid());
  // 引数で指定したシーケンス番号を入れる
  icp->un.echo.sequence = htons(sqc);
  ptr = (unsigned char *)&sbuff[ECHO_HDR_SIZE];
  psize = len - ECHO_HDR_SIZE;
  for (;psize;psize--) {
    *ptr ++= (unsigned char)0xA5;
  }
  ptr = (unsigned char *)&sbuff[ECHO_HDR_SIZE];
  // sendtimeをptrの指すアドレスに挿入する
  memcpy(ptr, sendtime, sizeof(struct timeval));
  // checksumを計算して格納する
  icp->checksum = calc_checksum((u_short *)icp, len);

  // 送信
  n = sendto(soc, sbuff, len, 0, &sa, sizeof(struct sockaddr));
  if (n == len) {
    return 0;
  } else {
    return -1000;
  }
}

int check_packet(char *rbuff,int nbytes,int len,struct sockaddr_in *from,unsigned short sqc,int *ttl,struct timeval *sendtime,struct timeval *recvtime,double *diff)
{
  struct iphdr *iph;
  struct icmphdr *icp;
  int i;
  unsigned char *ptr;

  // RTTを計算する
  *diff = (double)(recvtime->tv_sec-sendtime->tv_sec) + (double)(recvtime->tv_usec-sendtime->tv_usec) / 1000000.0;

  // 受信バッファに含まれるipヘッダ
  iph = (struct iphdr *)rbuff;
  *ttl = iph->ttl;

  // ICMPヘッダ
  icp = (struct icmphdr *)(rbuff + iph->ihl * 4);

  // バリデーション
  if (ntohs(icp->un.echo.id) != (unsigned short)getpid()) {
    return 1;
  }
  if (nbytes < len + iph->ihl * 4) {
    return -3000;
  }
  if (icp->type != ICMP_ECHOREPLY) {
    return -3010;
  }
  if (ntohs(icp->un.echo.sequence) != sqc) {
    return -3030;
  }

  ptr = (unsigned char *)(rbuff + iph->ihl * 4 + ECHO_HDR_SIZE);
  memcpy(sendtime, ptr, sizeof(struct timeval));
  ptr += sizeof(struct timeval);

  for (i = nbytes - iph->ihl * 4 - ECHO_HDR_SIZE - sizeof(struct timeval); i; i--) {
    if (*ptr++ != 0xA5) {
      return -3040;
    }
  }

  printf(
    "%d bytes from %s: icmp_seq=%d ttl=%d time=%.2f ms\n",
    nbytes-iph->ihl * 4,
    inet_ntoa(from->sin_addr),
    sqc,
    *ttl,
    *diff*1000.0
  );

  return 0;
}

int receive_ping(int soc,int len,unsigned short sqc,struct timeval *sendtime,int timeoutSec) {
  struct pollfd targets[1];
  double diff;
  int nready;
  int ret, nbytes, ttl;
  struct sockaddr_in from;
  socklen_t fromlen;
  struct timeval recvtime;
  char rbuff[BUFSIZE];

  memset(rbuff, 0, BUFSIZE);

  for (;;) {
    targets[0].fd = soc;
    targets[0].events = POLLIN | POLLERR;
    nready = poll(targets, 1, timeoutSec * 1000);

    if (nready == 0) {
      return -2000;
    }
    if (nready == -1) {
      if (errno == EINTR) {
        continue;
      } else {
        return -2010;
      }
    }

    // 受信
    fromlen = sizeof(from);
    nbytes = recvfrom(soc, rbuff, sizeof(rbuff), 0, (struct sockaddr *)&from, &fromlen);

    // 受信時刻を取得してrecvtimeに格納
    gettimeofday(&recvtime, NULL);

    // 受信したパケットの確認
    ret = check_packet(rbuff, nbytes, len, &from, sqc, &ttl, sendtime, &recvtime, &diff);

    switch (ret) {
      case 0:
        return (int)(diff * 1000.0);
      case 1:
        if (diff > timeoutSec * 1000) {
          // Timeout
          return -2000;
        }
        break;
      default:
        return -10000;
    }
  }
}

int ping_check(char *name,int len,int times,int timeoutSec)
{
  int soc;
  int i;
  struct timeval sendtime;
  int ret;
  int total = 0, total_no = 0;

  if ((soc = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0) {
    printf("errno=%d: %s\n", errno, strerror(errno));
    return -300;
  }

  for (i=0;i < times;i++) {
    ret = send_ping(soc, name, len, i + 1, &sendtime);
    if (ret == 0) {
      ret = receive_ping(soc, len, i + 1, &sendtime, timeoutSec);
      if (ret >= 0) {
        total += ret;
        total_no++;
      }
    }

    sleep(1);
  }

  close(soc);

  if (total_no > 0) {
    return total/total_no;
  } else {
    return -1;
  }
}

int main(int argc, char *argv[])
{
  int ret;

  if (argc < 2) {
    fprintf(stderr, "ping target\n");
    return EXIT_FAILURE;
  }

  ret = ping_check(argv[1], 64, 5, 1);

  if (ret >= 0) {
    printf("RTT:%dms\n", ret);
    return EXIT_SUCCESS;
  } else {
    printf("error:%d\n", ret);
    return EXIT_FAILURE;
  }
}
