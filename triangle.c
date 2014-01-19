#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "kyouko_regs.h"
#define KYOUKO_CONTROL_SIZE 0x1000

unsigned long buf;
unsigned int* u_buf;
unsigned int num_cmd, img_width, img_height;

struct _kyouko_device {
	unsigned int* control_base;
} kyouko;

unsigned int U_READ_REG(unsigned int reg) {
	return (*(kyouko.control_base + (reg>>2)));
}

void U_WRITE_REG(unsigned int reg, unsigned int val) {
	*(kyouko.control_base + (reg>>2)) = val;
}

float xdiff;
float ydiff;

float pixelPosition[4][4] = {
	0.0,0.0,0.0,1.0,
	1.0,0.0,0.0,1.0,
	1.0,1.0,0.0,1.0,
	0.0,1.0,0.0,1.0
};

void drawPixel(int x, int y, float color_1[]) {
	int i,j;
	float temp;
	for(i=0; i<4; i++){
		u_buf[num_cmd++] = (VtxColor);
		for(j=0;j<4;j++){
			u_buf[num_cmd++] = *(unsigned int*)&color_1[j];			
		}
		u_buf[num_cmd++] = (VtxPosition);
		temp = (((float)x*xdiff+pixelPosition[i][0]*xdiff)-1.0);
		u_buf[num_cmd++] = *(unsigned int*)&temp;
		temp = (((float)(img_height-1-y)*ydiff+pixelPosition[i][1]*ydiff)-1.0);
		u_buf[num_cmd++] = *(unsigned int*)&temp;
		u_buf[num_cmd++] = *(unsigned int*)&pixelPosition[i][2];
		u_buf[num_cmd++] = *(unsigned int*)&pixelPosition[i][3];
		u_buf[num_cmd++] = CmdVertex;
	}
}

float color[3][4] = {
	255.0,0.0,0.0,1.0,
	0.0,255.0,0.0,1.0,
	0.0,0.0,255.0,1.0,
};

float position[3][4] = {
	-0.75,0.75,0.0,1.0,
	0.25,0.25,0.0,1.0,
	-0.05,-0.75,0.0,1.0
};

float position1[3][4] = {
	-0.25,0.75,0.0,1.0,
	0.75,0.25,0.0,1.0,
	0.45,-0.75,0.0,1.0
};

float clearcolor [4] = {
	0.20,0.15,0.15,1.0
};

void triangle() {
	int i,j;
	U_WRITE_REG(CmdActiveBuffer,2);
	for(i=0;i<4;i++) U_WRITE_REG(VtxColor+(4*i), *(unsigned int*)&clearcolor[i]);
	while(U_READ_REG(InFIFO)>0);
	U_WRITE_REG(CmdClear,1);
	U_WRITE_REG(CmdPrimitive,4);
	for(i=0;i<3;i++){		
		for(j=0;j<4;j++) {
			U_WRITE_REG(VtxColor+(4*j), *(unsigned int*)&color[i][j]);		
		}
		for(j=0;j<4;j++) {
			U_WRITE_REG(VtxPosition+4*j, *(unsigned int*)&position[i][j]);
		}
		U_WRITE_REG(CmdVertex,1);
	}
	U_WRITE_REG(CmdPrimitive,0);
	U_WRITE_REG(CmdActiveBuffer,1);
}

void drawImage(int fd, char* filename, float srcX, float srcY){
	unsigned int pi,pj;
	FILE *fptr;
	char *c, buffer[512], *parse;
	int x, y;
	float pixelColor[4];
	if(!(fptr = fopen(filename, "r"))){
		printf("File opening failed!\n");
		return;
	}
	else{
		printf("File opened!\n");
		printf("Please wait, file is loading!\n");
		c = fgets(buffer, 512 ,fptr);
		parse = strtok(c, " ");
		img_width = atoi(parse);
		parse = strtok(NULL, " ");
		img_height = atoi(parse);

		xdiff = 2.0/img_width;
		ydiff = 2.0/img_height;
	
		for(pi=0; pi<img_width; pi++) 
			for(pj=0; pj<img_height; pj++){
			c = fgets(buffer, 512 ,fptr);
			parse = strtok(c, " ");
			x = atoi(parse);
			parse = strtok(NULL, " ");
			y = atoi(parse);
			parse = strtok(NULL, " ");
			pixelColor[0] = (atoi(parse)/255.0);
			parse = strtok(NULL, " ");
			pixelColor[1] = (atoi(parse)/255.0);
			parse = strtok(NULL, " ");
			pixelColor[2] = (atoi(parse)/255.0);
			pixelColor[3] = 1.0; 
			if((num_cmd<<2)>0xff00){
				buf = num_cmd;
				ioctl(fd, START_DMA, &buf);
				u_buf = (unsigned int*)buf;
				num_cmd=0;
			}
			drawPixel(srcX+x-1,srcY+y-1,pixelColor);
		}
		fclose(fptr);
	}
}

void image(int fd) {
	unsigned long i,j;
	u_buf = (unsigned int*)buf;
	num_cmd=0;
	u_buf[num_cmd++] = CmdActiveBuffer;
	u_buf[num_cmd++] = 2;

	u_buf[num_cmd++] = (VtxColor);
	for(i=0; i<4; i++) {	
		u_buf[num_cmd++] = *(unsigned int*)&clearcolor[i];
	}
	u_buf[num_cmd++] = CmdClear;
	u_buf[num_cmd++] = 1;
	u_buf[num_cmd++] = CmdPrimitive;
	u_buf[num_cmd++] = 8;
	drawImage(fd, "test2.txt",0,0);
	u_buf[num_cmd++] = CmdPrimitive;
	u_buf[num_cmd++] = 0;
	u_buf[num_cmd++] = CmdActiveBuffer;
	u_buf[num_cmd++] = 1;
	buf = num_cmd;
}

void triangles(int fd) {
	unsigned long i,j;
	u_buf = (unsigned int*)buf;
	num_cmd=0;
	u_buf[num_cmd++] = CmdActiveBuffer;
	u_buf[num_cmd++] = 2;

	u_buf[num_cmd++] = (VtxColor);
	for(i=0; i<4; i++) {	
		u_buf[num_cmd++] = *(unsigned int*)&clearcolor[i];
	}
	u_buf[num_cmd++] = CmdClear;
	u_buf[num_cmd++] = 1;
	u_buf[num_cmd++] = CmdPrimitive;
	u_buf[num_cmd++] = 5;
	for(i=0; i<3; i++){
		u_buf[num_cmd++] = (VtxColor);
		for(j=0;j<4;j++){
			u_buf[num_cmd++] = *(unsigned int*)&color[i][j];			
		}
		u_buf[num_cmd++] = (VtxPosition);
		for(j=0;j<4;j++){
			u_buf[num_cmd++] = *(unsigned int*)&position1[i][j];			
		}
		u_buf[num_cmd++] = CmdVertex;
	}
	for(i=0; i<3; i++){
		u_buf[num_cmd++] = (VtxColor);
		for(j=0;j<4;j++){
			u_buf[num_cmd++] = *(unsigned int*)&color[2-i][j];			
		}
		u_buf[num_cmd++] = (VtxPosition);
		for(j=0;j<4;j++){
			u_buf[num_cmd++] = *(unsigned int*)&position[i][j];			
		}
		u_buf[num_cmd++] = CmdVertex;
	}
	u_buf[num_cmd++] = CmdPrimitive;
	u_buf[num_cmd++] = 0;
	u_buf[num_cmd++] = CmdActiveBuffer;
	u_buf[num_cmd++] = 1;
	buf = num_cmd;
}

int main() {
	int result,i,choice;
	int fd = open("/dev/kyouko",O_RDWR);
	system("clear");
	do{
		printf("\t\t\t\tMENU\n\t\t\t1) For FIFO Mode\n\t\t\t2) For triangle using DMA\n\t\t\t3) For image using DMA\n\t\t\t4) Exit\n\n\t\t\tEnter your choice:");
		scanf("%d",&choice);
		system("clear");
	switch(choice){
		case 1:
			kyouko.control_base = mmap(0,KYOUKO_CONTROL_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
			if((unsigned long)kyouko.control_base == 0xffffffffffffffff) {
				printf("FIFO access allowed to only root user!\n");
				return -1;
			}
			ioctl(fd, VMODE, GRAPHICS_ON);
			triangle();
			sleep(5); 
			ioctl(fd, VMODE, GRAPHICS_OFF);
			if(munmap(kyouko.control_base,KYOUKO_CONTROL_SIZE)==-1)
				error("munmap failed with error: ");
			break;
		case 2:
			ioctl(fd, VMODE, GRAPHICS_ON);
			ioctl(fd, BIND_DMA, &buf);
			triangles(fd);
			ioctl(fd, START_DMA, &buf);
			sleep(5); 
			ioctl(fd, VMODE, GRAPHICS_OFF);
			break;
		case 3:
			ioctl(fd, VMODE, GRAPHICS_ON);
			ioctl(fd, BIND_DMA, &buf);
			image(fd);
			ioctl(fd, START_DMA, &buf);
			sleep(5); 
			ioctl(fd, VMODE, GRAPHICS_OFF);
			break;
		case 4: break;
		default:
			printf("please enter proper choice...\n");
	}
	}while(choice!=4);
	close(fd);
	return 0;
}
