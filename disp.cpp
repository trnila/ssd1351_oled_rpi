#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <gpiod.h>
#include <limits.h>
#include "a.h"

struct display {
	int spi_cdev;
	struct gpiod_line *res;
	struct gpiod_line *dc;
};

void display_xmit(struct display *disp, const uint8_t *data, size_t len) {
	if(write(disp->spi_cdev, data, len) != len) {
		perror("spi write failed: ");
		exit(1);
	}
}

void display_data(struct display *disp, const uint8_t *data, size_t len) {
	if(gpiod_line_set_value(disp->dc, 1) < 0) {
		perror("failed to set gpio");
		exit(1);
	}
	display_xmit(disp, data, len);
}
void display_cmd(struct display *disp, const uint8_t *data, size_t len) {
	if(gpiod_line_set_value(disp->dc, 0) < 0) {
		perror("failed to set gpio");
		exit(1);
	}
	display_xmit(disp, data, 1);
	if(len > 1) {
		display_data(disp, data+1, len - 1);
	}
}

uint16_t to_color(uint8_t r, uint8_t g, uint8_t b) {
	return ((b >> 3) << 3) | ((r >> 3) << 8) | (g >> 3 >> 2) | ((g >> 5) << 5 << 8);
}


int open_spi(int dev, int cs) {
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "/dev/spidev%d.%d", dev, cs);

	int fd = open(path, O_RDWR);
	if(fd < 0) {
		perror("failed to open spidev: ");
		exit(1);
	}

	uint32_t speed = 20000000;
	if(ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) == -1) {
		perror("ioctl");
		exit(1);
	}

	return fd;
}

void ssd1351_init(struct display *disp) {
	const int width = 128;
	const int height = 128;
        display_cmd(disp, (const uint8_t[]){0xFD, 0x12}, 2);               // Unlock IC MCU interface
        display_cmd(disp, (const uint8_t[]){0xFD, 0xB1}, 2);               // Command A2,B1,B3,BB,BE,C1 accessible if in unlock state
        display_cmd(disp, (const uint8_t[]){0xAE}, 1);                     // Display off
        display_cmd(disp, (const uint8_t[]){0xB3, 0xF1}, 2);               // Clock divider
        display_cmd(disp, (const uint8_t[]){0xCA, 0x7F}, 2);               // Mux ratio
        display_cmd(disp, (const uint8_t[]){0x15, 0x00, width - 1}, 3);    // Set column address
        display_cmd(disp, (const uint8_t[]){0x75, 0x00, height - 1}, 3);   // Set row address
        display_cmd(disp, (const uint8_t[]){0xA0, 0x70 | 0x00}, 2);  // Segment remapping
        display_cmd(disp, (const uint8_t[]){0xA1, 0x00}, 2);               // Set Display start line
        display_cmd(disp, (const uint8_t[]){0xA2, 0x00}, 2);               // Set display offset
        display_cmd(disp, (const uint8_t[]){0xB5, 0x00}, 2);               // Set GPIO
        display_cmd(disp, (const uint8_t[]){0xAB, 0x01}, 2);               // Function select (internal - diode drop);
        display_cmd(disp, (const uint8_t[]){0xB1, 0x32}, 2);               // Precharge
        display_cmd(disp, (const uint8_t[]){0xB4, 0xA0, 0xB5, 0x55}, 4);   // Set segment low voltage
        display_cmd(disp, (const uint8_t[]){0xBE, 0x05}, 2);               // Set VcomH voltage
        display_cmd(disp, (const uint8_t[]){0xC7, 0x0F}, 2);               // Contrast master
        display_cmd(disp, (const uint8_t[]){0xB6, 0x01}, 2);               // Precharge2
        display_cmd(disp, (const uint8_t[]){0xA6}, 1);                     // Normal display
//	display_cmd(,(condispst uint8_t[]){0xc1, 0xff, 0xff, 0xff}, 4);
        display_cmd(disp, (const uint8_t[]){0xAf}, 1);                     // Display off

        display_cmd(disp, (const uint8_t[]){0x15, 0, 127}, 3);                     // Write RAM
        display_cmd(disp, (const uint8_t[]){0x75, 0, 127}, 3);                     // Write RAM
        display_cmd(disp, (const uint8_t[]){0x5C}, 1);                     // Write RAM
}

int main() {
	struct gpiod_chip *chip = gpiod_chip_open_by_name("gpiochip0");
	if(!chip) {
		perror("failed to open gpiochip: ");
		exit(1);
	}

	// RES 25
	// DC 24
	struct gpiod_line *res = gpiod_chip_get_line(chip, 25);
	if(!res) {
		perror("Failed to get chipline: ");
		exit(1);
	}	
	if(gpiod_line_request_output(res, "reset", 1) < 0) {
		perror("Failed to allocate output line");
		exit(1);
	}

	struct gpiod_line *dc = gpiod_chip_get_line(chip, 24);
	if(!res) {
		perror("Failed to get chipline: ");
		exit(1);
	}	
	if(gpiod_line_request_output(dc, "dc", 0) < 0) {
		perror("Failed to allocate output line");
		exit(1);
	}

	struct display first;
	first.spi_cdev = open_spi(0, 0);
	first.res = res;
	first.dc = dc;

	if(gpiod_line_set_value(res, 0) < 0) {
		perror("line set value");
		exit(1);
	}
	usleep(2);
	if(gpiod_line_set_value(res, 1) < 0) {
		perror("line set value");
		exit(1);
	}
	usleep(2);

	ssd1351_init(&first);

//	memset(fb, 0x00, 128*128 * 2);
	int off = 0;
	for(;;) {
		uint16_t fb[128*128]{};
		int i = 0;
		srand(time(NULL));
		int j = 0;
		for(int r = off; r < off+20; r++) {
			for(int c = off; c < off+20; c++) {
				fb[r * 128 + c] = to_color(0, 255, 0);
				//fb[i] = to_color(MagickImage[j], MagickImage[j+1], MagickImage[j+2]);
				i++;
				j += 3;
			}
		}
		int sent = 0;
		int len = sizeof(fb);
		while(sent < len) {
			int rem = len - sent;
			if(rem > 4096) {
//				rem = 4096;
			}
			display_data(&first, ((const uint8_t*) fb) + sent , rem);	
			sent += rem;
		} 
		off++;
		if(off > 50) {
			off = 0;
		}
		usleep(10000);
	}
}
