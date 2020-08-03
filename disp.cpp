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
#include <sys/mman.h>

struct display {
	int spi_cdev;
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

struct video {
	uint8_t *mem;
	size_t frames;
};

struct video load_video(const char *path) {
	int fd = open(path, O_RDONLY);
	if(fd < 0) {
		perror("open");
		exit(1);
	}
	off_t size = lseek(fd, 0, SEEK_END);
	uint8_t *videom = (uint8_t*) mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
	if(videom == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	struct video video;
	video.mem = videom;
	video.frames = size / (128*128*2);
	return video;
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

	// reset all displays
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


	struct display displays[2];
	displays[0].spi_cdev = open_spi(0, 0);
	displays[0].dc = dc;
	ssd1351_init(&displays[0]);

	displays[1].spi_cdev = open_spi(0, 1);
	displays[1].dc = dc;
	ssd1351_init(&displays[1]);


	struct video vid1 = load_video("out");
	struct video vid2 = load_video("vsb.raw");

	size_t frame = 0;
	int fps = 30;
	for(;;) {
		struct timespec start, end;
		clock_gettime(CLOCK_MONOTONIC, &start);
		display_data(&displays[0], vid1.mem + 128*128*2*(frame % vid1.frames) , 128*128*2);	
		display_data(&displays[1], vid2.mem + 128*128*2*(frame % vid2.frames) , 128*128*2);	
		frame++;

		clock_gettime(CLOCK_MONOTONIC, &end);
		uint64_t elapsed_us = 0;
		
		if((end.tv_nsec - start.tv_nsec) < 0) {
			elapsed_us = (end.tv_sec - start.tv_sec - 1UL) * 1000000UL + (1000000000UL + end.tv_nsec - start.tv_nsec) / 1000UL;
		} else {
			elapsed_us = (end.tv_sec - start.tv_sec) * 1000000UL + (end.tv_nsec - start.tv_nsec) / 1000UL;
		}

		int t_us = 1000000 / fps - elapsed_us;
		printf("%llu %d\n", elapsed_us, t_us);
		if(t_us > 1000) {
			usleep(t_us);
		}
	}
}
