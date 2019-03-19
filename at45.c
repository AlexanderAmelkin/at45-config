/*
 * Copyright (C) 2019 Alexander Amelkin <alexander@amelkin.msk.ru>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <getopt.h>

#define JEDEC_ID_CMD 0x9F
#define AT45_STATUS_CMD 0xD7
#define AT45_PAGE_256 0xA6
#define AT45_PAGE_264 0xA7
#define AT45_SET_PAGE_SZ 0x3D, 0x2A, 0x80

#define ARRAY_SZ(x) (sizeof(x) / sizeof((x)[0]))
#define SPI_XFER(arr) SPI_IOC_MESSAGE(ARRAY_SZ(arr)), (arr)

#define SPI_SPEED_HZ 40000000
#define DEFAULT_SPIDEV "/dev/spidev0.0"
#define SPI_CMD_DELAY 100000

struct {
	uint32_t jedec_id;
	char *name;
} chips[] = {
	0x0100241F, "Adesto AT45DB041E",
	0, NULL /* End of chips */
};

struct {
	char *descr[2]; /* false, true */
} status_bits[] = {
	[0] = { "Device is configured for standard DataFlash page size (264 bytes)",
		"Device is configured for 'power of 2' binary page size (256 bytes)" },
	[1] = { "Sector protection is disabled",
		"Sector protection is enabled" },
	[2] = { "Unknown density", "4-Mbit" },
	[3] = { "Unknown density", "4-Mbit" },
	[4] = { "Unknown density", "4-Mbit" },
	[5] = { "4-Mbit", "Unknown density" },
	[6] = { "Main memory page data matches buffer data",
		"Main memory page data does not match buffer data" },
	[7] = { "Device is busy with an internal operation",
		"Device is ready" },
	[8] = { "No sectors are erase suspended",
		"A sector is erase suspended" },
	[9] = { "No program operation has been suspended while using Buffer 1",
		"A sector is program suspended while using Buffer 1" },
	[10] = { "No program operation has been suspended while using Buffer 2",
		 "A sector is program suspended while using Buffer 2" },
	[11] = { "Sector Lockdown command is disabled",
		 "Sector Lockdown command is enabled" },
	[12] = { "Reserved", "Reserved" },
	[13] = { "Erase or program operation was successful",
		 "Erase or program error detected" },
	[14] = { "Reserved", "Reserved" },
	[15] = { "Device is busy with an internal operation",
		 "Device is ready" },
};

#define DEF_SPI_CMD(cmd, snd, rcv) \
	struct spi_ioc_transfer cmd[2] = { \
		{ \
			.tx_buf = (uintptr_t)(snd), \
			.rx_buf = (uintptr_t)(rcv), \
			.len = sizeof(snd), \
			.tx_nbits = CHAR_BIT * sizeof(snd), \
			.rx_nbits = CHAR_BIT * sizeof(rcv), \
			.cs_change = 1, \
			.speed_hz = SPI_SPEED_HZ \
		} \
	}

#define DO_XFER(cmd, rval) \
	if (0 > ioctl(fd, SPI_XFER(cmd))) { \
		perror(__func__); \
		return (rval); \
	}


uint32_t get_jedec_id(int fd)
{
	uint8_t send_data[6] = { JEDEC_ID_CMD };
	uint8_t recv_data[6] = { 0 };
	DEF_SPI_CMD(jedec_id, send_data, recv_data);
	DO_XFER(jedec_id, UINT32_MAX);

	return *(uint32_t *)(recv_data + 1);
}

int at45_get_status(int fd)
{
	uint8_t send_data[3] = { AT45_STATUS_CMD };
	uint8_t recv_data[3] = { 0 };
	DEF_SPI_CMD(status, send_data, recv_data);
	DO_XFER(status, -1);

	return *(uint16_t *)(recv_data + 1);
}

bool at45_set_page_sz(int fd, uint8_t page_sz)
{
	uint8_t send_data[4] = { AT45_SET_PAGE_SZ, 0 };
	uint8_t recv_data[4] = { 0 }; /* Ignore */
	DEF_SPI_CMD(set_page_sz, send_data, recv_data);
	send_data[3] = page_sz;
	DO_XFER(set_page_sz, true);

	return false;
}

int main(int argc, char *argv[])
{
	int fd;
	int ret = EXIT_FAILURE;
	int id;
	int i;
	int opt;
	char *devname = DEFAULT_SPIDEV; /* Default is SPI0 CS0 */
	int pagesize = 0; /* Don't set page size by default */
	bool show_status;
	struct option options[] = {

		{ "spidev", true, NULL, 'd' },
		{ "pagesize", true, NULL, 'p' },
		{ "status", false, NULL, 's' },
		{ "help", false, NULL, 'h' },

	};

	while ((opt = getopt_long(argc, argv, "d:p:sh", options, &i)) != -1) {
		switch (opt) {
		case 'd':
			devname = optarg;
			break;
		case 'p':
			if (!strcmp(optarg, "256")) {
				pagesize = AT45_PAGE_256;
			}
			else {
				pagesize = AT45_PAGE_264;
			}
			break;
		case 's':
			show_status = true;
			break;
		case 'h':
			ret = EXIT_SUCCESS;
			/* fall through */
		default:
			printf("Usage: %s [<options>]\n", argv[0]);
			printf("\tOptions:\n");
			printf("\t\t--spidev, -d <device>  - Use <device>, default is %s\n",
			       DEFAULT_SPIDEV);
			printf("\t\t--help, -h             - Show this help\n");
			goto out;
		}
	}

	printf("Using device %s\n", devname);
	fd = open(devname, O_RDWR);
	if (fd < 0) {
		perror("open");
		goto out;
	}

	id = get_jedec_id(fd);

	for (i = 0; chips[i].name; ++i) {
		printf("Checking %s...\n", chips[i].name);
		if (chips[i].jedec_id == id) {
			printf("Found %s\n", chips[i].name);
			break;
		}
	}

	if (!chips[i].name) {
		printf("No supported chips found (id = 0x%08X)\n", id);
		goto out;
	}

	if (pagesize) {
		bool err;
		err = at45_set_page_sz(fd, pagesize);
		if (err) {
			printf("Failed to set page size\n");
			goto out;
		}
		/* Wait to let subsequent status request show the change */
		usleep(SPI_CMD_DELAY);
	}

	if (show_status) {
		int status = at45_get_status(fd);
		if (status < 0) {
			printf("Failed to get status\n");
			goto out;
		}

		printf("Status: %04X\n", status);
		for (i = 15; i >= 0; --i) {
			bool value = (status >> i) & 1;
			printf("\t[%02d]: %d = %s\n",
			       i, value, status_bits[i].descr[value]);
		}
	}

	ret = EXIT_SUCCESS;
out:
	return ret;
}
