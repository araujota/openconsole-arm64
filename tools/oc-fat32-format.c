/* SPDX-License-Identifier: Apache-2.0 */
/* Minimal, deterministic FAT32 formatter for an already-created partition. */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static void le16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void le32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static int write_at(FILE *f,uint64_t off,const void *data,size_t n){
#ifdef _WIN32
    return _fseeki64(f,(__int64)off,SEEK_SET)==0&&fwrite(data,1,n,f)==n;
#else
    return fseeko(f,(off_t)off,SEEK_SET)==0&&fwrite(data,1,n,f)==n;
#endif
}
static uint64_t file_size(FILE *f){
#ifdef _WIN32
    GET_LENGTH_INFORMATION length;DWORD returned=0;HANDLE handle=(HANDLE)_get_osfhandle(_fileno(f));
    if(DeviceIoControl(handle,IOCTL_DISK_GET_LENGTH_INFO,0,0,&length,sizeof(length),&returned,0))return (uint64_t)length.Length.QuadPart;
    {__int64 here=_ftelli64(f),end;if(_fseeki64(f,0,SEEK_END))return 0;end=_ftelli64(f);_fseeki64(f,here,SEEK_SET);return (uint64_t)end;}
#else
    off_t here=ftello(f),end;if(fseeko(f,0,SEEK_END))return 0;end=ftello(f);fseeko(f,here,SEEK_SET);return (uint64_t)end;
#endif
}
int main(int argc,char **argv){
    FILE *f;uint64_t bytes,sectors,data_sectors,clusters;uint32_t fat_sectors=1,needed;
    uint32_t bps=512,spc=1,reserved=32,root=2,i;uint8_t *sector,*zero;char label[12]="NO NAME    ";
    if(argc<2||argc>4){fprintf(stderr,"usage: oc-fat32-format DEVICE_OR_IMAGE [LABEL] [SECTOR_BYTES]\n");return 2;}
    if(argc>=3){size_t n=strlen(argv[2]);if(n<1||n>11){fprintf(stderr,"label must be 1..11 ASCII bytes\n");return 2;}memset(label,' ',11);memcpy(label,argv[2],n);label[11]=0;}
    if(argc==4){unsigned long value=strtoul(argv[3],0,10);if(value!=512&&value!=4096){fprintf(stderr,"sector must be 512 or 4096\n");return 2;}bps=(uint32_t)value;}
    f=fopen(argv[1],"r+b");if(!f){fprintf(stderr,"open: %s\n",strerror(errno));return 2;}bytes=file_size(f);sectors=bytes/bps;
    if(bytes<64ull*1024*1024||bytes%bps||sectors>0xffffffffull){fprintf(stderr,"unsupported FAT32 capacity\n");fclose(f);return 2;}
    while(sectors/spc>0x0ffffff5ull&&spc<64)spc*=2;
    while(1){data_sectors=sectors-reserved-2ull*fat_sectors;clusters=data_sectors/spc;needed=(uint32_t)(((clusters+2ull)*4ull+bps-1)/bps);if(needed==fat_sectors)break;fat_sectors=needed;}
    if(clusters<65525ull||clusters>0x0ffffff5ull){fprintf(stderr,"invalid FAT32 cluster count\n");fclose(f);return 2;}
    sector=(uint8_t*)calloc(1,bps);zero=(uint8_t*)calloc(1,(size_t)bps*spc);if(!sector||!zero){fclose(f);free(sector);free(zero);return 2;}
    sector[0]=0xeb;sector[1]=0x58;sector[2]=0x90;memcpy(sector+3,"OPENCONS",8);le16(sector+11,(uint16_t)bps);sector[13]=(uint8_t)spc;le16(sector+14,(uint16_t)reserved);sector[16]=2;le16(sector+17,0);le16(sector+19,0);sector[21]=0xf8;le16(sector+22,0);le16(sector+24,63);le16(sector+26,255);le32(sector+28,0);le32(sector+32,(uint32_t)sectors);le32(sector+36,fat_sectors);le16(sector+40,0);le16(sector+42,0);le32(sector+44,root);le16(sector+48,1);le16(sector+50,6);sector[64]=0x80;sector[66]=0x29;le32(sector+67,0x4f434f4e);memcpy(sector+71,label,11);memcpy(sector+82,"FAT32   ",8);sector[510]=0x55;sector[511]=0xaa;
    if(!write_at(f,0,sector,bps)||!write_at(f,6ull*bps,sector,bps)){fprintf(stderr,"boot write failed\n");return 2;}
    memset(sector,0,bps);le32(sector,0x41615252);le32(sector+484,0x61417272);le32(sector+488,(uint32_t)(clusters-1ull));le32(sector+492,3);le32(sector+508,0xaa550000);
    if(!write_at(f,bps,sector,bps)||!write_at(f,7ull*bps,sector,bps)){fprintf(stderr,"FSInfo write failed\n");return 2;}
    memset(sector,0,bps);le32(sector,0x0ffffff8);le32(sector+4,0xffffffff);le32(sector+8,0x0fffffff);
    for(i=0;i<2;i++){uint64_t start=(reserved+(uint64_t)i*fat_sectors)*bps;if(!write_at(f,start,sector,bps)){fprintf(stderr,"FAT header write failed\n");return 2;}for(uint32_t j=1;j<fat_sectors;j++)if(!write_at(f,start+(uint64_t)j*bps,zero,bps)){fprintf(stderr,"FAT clear failed\n");return 2;}}
    memcpy(zero,label,11);zero[11]=0x08;
    if(!write_at(f,(reserved+2ull*fat_sectors)*bps,zero,(size_t)bps*spc)||fflush(f)){fprintf(stderr,"root/flush failed\n");return 2;}
#ifndef _WIN32
    if(fsync(fileno(f))){fprintf(stderr,"fsync failed\n");return 2;}
#endif
    fclose(f);free(sector);free(zero);printf("FAT32 sectors=%llu clusterSectors=%u fatSectors=%u\n",(unsigned long long)sectors,spc,fat_sectors);return 0;
}
