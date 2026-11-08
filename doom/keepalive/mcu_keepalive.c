/* mcu_keepalive: keep the ATOTO MCU happy while appman is dead.
 * Sends one or more fixed glink frames to the MCU UART every interval.
 * Usage: mcu_keepalive <dev> <baud> <interval_ms> <framehex1> [framehex2 ...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

static speed_t baud_const(int b){
    switch(b){case 57600:return B57600;case 115200:return B115200;
        case 230400:return B230400;case 460800:return B460800;
        case 921600:return B921600;default:return B115200;}
}
int main(int argc,char**argv){
    if(argc<5){fprintf(stderr,"usage: %s dev baud interval_ms framehex...\n",argv[0]);return 2;}
    const char*dev=argv[1]; int baud=atoi(argv[2]); int interval_ms=atoi(argv[3]);
    int nf=argc-4;
    unsigned char buf[16][256]; int len[16];
    if(nf>16) nf=16;
    for(int f=0;f<nf;f++){
        const char*hex=argv[4+f]; int n=strlen(hex)/2; len[f]=n;
        for(int i=0;i<n;i++){unsigned v=0;sscanf(hex+2*i,"%2x",&v);buf[f][i]=(unsigned char)v;}
    }
    int fd=open(dev,O_RDWR|O_NOCTTY);
    if(fd<0){perror("open");return 1;}
    struct termios t; tcgetattr(fd,&t);
    t.c_cflag=baud_const(baud)|CLOCAL|CREAD|CS8; t.c_iflag=IGNPAR; t.c_oflag=0; t.c_lflag=0;
    cfsetospeed(&t,baud_const(baud)); cfsetispeed(&t,baud_const(baud));
    tcflush(fd,TCIFLUSH); tcsetattr(fd,TCSANOW,&t);
    fprintf(stderr,"keepalive dev=%s baud=%d interval=%dms frames=%d\n",dev,baud,interval_ms,nf);
    for(;;){
        for(int f=0;f<nf;f++){ if(write(fd,buf[f],len[f])!=len[f]) perror("write"); usleep(20000);}
        usleep((useconds_t)interval_ms*1000);
    }
    return 0;
}
