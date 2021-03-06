#include <fcntl.h>
#include <tclap/ArgException.h>
#include <tclap/CmdLine.h>
#include <tclap/SwitchArg.h>
#include <tclap/ValueArg.h>
#include <termios.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "states.h"

int main(int argc, char *argv[]) {
	/* parse command-line options */
	std::string port;
	std::string file;
	bool run;
	bool reboot;
	bool debug;
	std::vector<TCLAP::Arg*> xorList;
	try {
		TCLAP::CmdLine cmd("PIC bootloader loader", ' ', "0.1");
		TCLAP::ValueArg<std::string> port_arg("p", "port", "PIC serial port",
				true, "/dev/ttyO1", "serial port");
		cmd.add(port_arg);
		TCLAP::ValueArg<std::string> file_arg("f", "file", "HEX file", false,
				"", "HEX file");
		xorList.push_back(&file_arg);
		TCLAP::SwitchArg run_arg("x", "run", "Run program", false);
		xorList.push_back(&run_arg);
		TCLAP::SwitchArg reboot_arg("r", "reboot", "Reboot PIC", false);
		xorList.push_back(&reboot_arg);
		cmd.xorAdd(xorList);
		TCLAP::SwitchArg debug_arg("d", "debug", "Print debug messages", false);
		cmd.add(debug_arg);
		cmd.parse(argc, argv);
		port = port_arg.getValue();
		file = file_arg.getValue();
		run = run_arg.getValue();
		reboot = reboot_arg.getValue();
		debug = debug_arg.getValue();
	} catch (TCLAP::ArgException &e) {
		std::cerr << "error: " << e.error() << " for arg " << e.argId()
				<< std::endl;
	}

	/* open serial port */
	int fd = open(port.c_str(), O_RDWR | O_NOCTTY);
	if (fd == -1) {
		fprintf(stderr, "%s: unable to open %s\n", __FUNCTION__, port.c_str());
		exit(EXIT_FAILURE);
	}

	/* backup terminal settings */
	struct termios termios_original;
	tcgetattr(fd, &termios_original);

	/* configure terminal settings */
	struct termios termios = termios_original;
	termios.c_iflag = 0;
	termios.c_oflag = 0;
	termios.c_cflag = B115200 | CS8 | CREAD | CLOCAL;
	termios.c_lflag = 0;
	termios.c_lflag &= ~ICANON;
	termios.c_cc[VMIN] = 1;
	termios.c_cc[VTIME] = 0;
	tcsetattr(fd, TCSANOW, &termios);

	char c;
	if (run) {
		/* execute previously loaded program */
		c = STATE_BOOT;
		write(fd, &c, 1);
		read(fd, &c, 1);
		printf("run status = %c\n", c);
	} else if (reboot) {
		/* reboot PIC */
		c = STATE_REBOOT;
		write(fd, &c, 1);
		read(fd, &c, 1);
		printf("reboot status = %c\n", c);
	} else {
		/* erase flash */
		c = STATE_ERASE_FLASH;
		write(fd, &c, 1);
		read(fd, &c, 1);
		printf("erase status = %c\n", c);

		/* load new program */
		static const unsigned int buffer_size = 1 + 2 + 4 + 2 + 256 * 2 + 2 + 1;
		char buffer[buffer_size];
		FILE *fp = fopen(file.c_str(), "r");
		while (fgets(buffer, buffer_size, fp)) {
			if (buffer[0] == ':') {
				/* found record line */
				static const unsigned int num_tries = 10;
				bool failed = true;
				for (unsigned int count = 0; count < num_tries; count++) {
					if (debug) {
						printf("Sending %s", buffer);
					}
					/* send record line */
					for (char *ptr = buffer; ((*ptr != '\n') && (*ptr != '\0'));
							ptr++) {
						write(fd, ptr, 1);
					}
					/* receive checksum status */
					read(fd, &c, 1);
					if (c == '0') {
						/* checksum OK */
						if (debug) {
							printf("OK\n");
						}
						failed = false;
						break;
					} else {
						if (debug) {
							printf("KO\n");
						}
					}
				}
				if (failed) {
					goto end;
				}
			}
		}
		fclose(fp);
	}

	end:
	/* restore terminal settings */
	tcsetattr(fd, TCSANOW, &termios_original);
	close(fd);

	exit(EXIT_SUCCESS);
}
