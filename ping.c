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
  // nop
}

int recv_ping(int soc,int len,unsigned short sqc,struct timeval *sendtime,int timeoutSec)
{
  // nop
}

int ping_check(char *name,int len,int times,int timeoutSec)
{
  // nop
}

int main(int argc, char *argv[])
{
  return 0;
}
