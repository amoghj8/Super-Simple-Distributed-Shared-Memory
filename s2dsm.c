#define _GNU_SOURCE
#include <sys/types.h>
#include <stdio.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#define BUFF_SIZE 4096
#define TEMP_BUFF_SIZE 50
#define errExit(msg) do {
	perror(msg);
	exit(EXIT_FAILURE);\
} while (0)

struct Payload
{
	int page_number;
	char cmd;
	char flag;
	char content[BUFF_SIZE + 1];
};

static int pages;
static int page_size;
static int first;
char *msi_array;
struct Payload response;
char *addr;
int main_sock;

void get_content(int page, char *field)
{
	int start_index = page * page_size;
	char temp[BUFF_SIZE + 1];
	memset(temp, '\0', (BUFF_SIZE + 1) *(sizeof(char)));
	for (int i = 0; i < page_size; i++)
	{
		temp[i] = addr[start_index + i];
	}
	strncpy(field, temp, BUFF_SIZE + 1);
}

static void *check_page(void *arg)
{
	int temp_socket = *(int*) arg;
	struct Payload received, payload_sent;

	for (;;)
	{
		if (read(temp_socket, &received, sizeof(received)) < 0)
		{
			printf("\nRead Failed \n");
			return NULL;
		}
		else
		{

			if (received.flag == 'R')
			{

				if (msi_array[received.page_number] == 'I')
				{
					payload_sent.cmd = msi_array[received.page_number];
					payload_sent.page_number = received.page_number;
					payload_sent.flag = 'P';
					send(temp_socket, &payload_sent, sizeof(payload_sent), 0);
				}
				else
				{
					if (received.page_number == -1)
					{
						memset(msi_array, 'S', pages* sizeof(msi_array[0]));
					}
					else
					{
						msi_array[received.page_number] = 'S';
					}
					payload_sent.cmd = 'S';
					payload_sent.page_number = received.page_number;
					payload_sent.flag = 'P';
					get_content(received.page_number, payload_sent.content);
					send(temp_socket, &payload_sent, sizeof(payload_sent), 0);
				}
			}
			else if (received.flag == 'W')
			{

				if (received.page_number == -1)
				{
					memset(msi_array, received.cmd, pages* sizeof(msi_array[0]));
				}
				else
				{
					msi_array[received.page_number] = received.cmd;
				}
			}
			else
			{
				response = received;
			}
		}
	}
}

static void *handle_fault(void *arg)
{
	static struct uffd_msg msg;
	long uffd;
	static char *page = NULL;
	struct uffdio_copy uffdio_copy;
	ssize_t nread;

	uffd = (long) arg;
	if (page == NULL)
	{
		page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (page == MAP_FAILED)
		{
			errExit("mmap");
		}
	}

	for (;;)
	{
		struct pollfd pollfd;
		int nready;

		pollfd.fd = uffd;
		pollfd.events = POLLIN;
		nready = poll(&pollfd, 1, -1);
		if (nready == -1)
			errExit("poll");

		nread = read(uffd, &msg, sizeof(msg));
		if (nread == 0)
		{
			printf("EOF on userfaultfd!\n");
			exit(EXIT_FAILURE);
		}

		if (nread == -1)
			errExit("read");

		if (msg.event != UFFD_EVENT_PAGEFAULT)
		{
			fprintf(stderr, "Unexpected event on userfaultfd\n");
			exit(EXIT_FAILURE);
		}

		printf("[x] PAGEFAULT\n");

		memset(page, '\0', page_size);

		uffdio_copy.src = (unsigned long) page;
		uffdio_copy.dst = (unsigned long) msg.arg.pagefault.address &~(page_size - 1);
		uffdio_copy.len = page_size;
		uffdio_copy.mode = 0;
		uffdio_copy.copy = 0;

		if (ioctl(uffd, UFFDIO_COPY, &uffdio_copy) == -1)
			errExit("ioctl-UFFDIO_COPY");
	}
}

void read_pages(int page_range)
{
	int l;
	int loop_end = 1;
	int start_index;
	if (page_range != -1)
		start_index = page_size * page_range;
	else
		start_index = 0;
	if (page_range == -1)
	{
		loop_end = pages;
		for (l = 0; l < loop_end; l++)
		{
			printf("[*] Page %i:\n", l);
			for (int k = 0; k < page_size; k++)
			{
				if (addr[start_index + k] == '\0')
					break;
				printf("%c", addr[start_index + k]);
			}

			printf("\n");
			start_index += page_size;
		}
	}
	else
	{
		char temp[page_size + 1];
		for (int k = 0; k < page_size; k++)
		{
			temp[k] = addr[start_index + k];
		}

		temp[page_size] = '\0';
		printf("[*] Page %i:\n%s\n", page_range, temp);
	}
}

void write_pages(int page_range, char *input)
{
	int l = 0;
	int loop_end = 1;
	int start_index;
	if (page_range == -1)
	{
		start_index = 0;
	}
	else
	{
		start_index = page_size * page_range;
	}

	if (page_range == -1)
	{
		loop_end = pages;
		int input_string_length = (int) strlen(input);
		for (l = 0; l < loop_end; l++)
		{
			strcpy(&addr[start_index], input);
			addr[start_index + input_string_length] = '\0';
			printf("[*] Page %i:\n", l);
			for (int k = 0; k < page_size; k++)
			{
				if (addr[start_index + k] == '\0')
					break;
				printf("%c", addr[start_index + k]);
			}

			start_index += page_size;
			printf("\n");
		}
	}
	else
	{
		strcpy(&addr[start_index], input);
		addr[start_index + strlen(input)] = '\0';
		printf("[*] Page %i:\n", page_range);
		while (addr[start_index] != '\0')
		{
			printf("%c", addr[start_index++]);
		}

		printf("\n");
	}
}

void view_msi_array(int pages)
{
	if (pages == -1)
	{
		int i = 0;
		printf("Printing MSI vlaues for all pages\n");
		while (msi_array[i] != '\0')
		{
			printf("MSI value for page %d : %c\n", i, msi_array[i]);
			i++;
		}
	}
	else
	{
		printf("MSI value for page %d : %c\n", pages, msi_array[pages]);
	}
}

void invalidate_pages(char *addr, int page, unsigned long total_size)
{
	if (page == -1)
	{
		if (madvise(addr, total_size, MADV_DONTNEED))
		{
			errExit("fail to madvise");
		}
	}
	else
	{
		int start_index = page * page_size;
		if (madvise(addr + start_index, page_size, MADV_DONTNEED))
		{
			errExit("fail to madvise");
		}
	}
}

void read_handler(int i, unsigned long total_size)
{
	if (msi_array[i] == 'I')
	{
		read_pages(i);
		memset(response.content, '\0', (BUFF_SIZE + 1) *sizeof(char));
		struct Payload payload;
		payload.cmd = 'I';
		payload.flag = 'R';
		payload.page_number = i;
		send(main_sock, &payload, sizeof(payload), 0);
		sleep(3);
		if (response.cmd != 'I')
		{
			write_pages(i, response.content);
			msi_array[i] = 'S';
		}
		else
		{
			if (madvise(addr, page_size, MADV_DONTNEED))
			{
				errExit("fail to madvise");
			}
			invalidate_pages(addr, i, total_size);
		}
	}
	else
	{
		read_pages(i);
	}
}

void write_handler(int i, char *input)
{
	msi_array[i] = 'M';
	write_pages(i, input);
	struct Payload payload;
	payload.cmd = 'I';
	payload.flag = 'W';
	payload.page_number = i;
	send(main_sock, &payload, sizeof(payload), 0);
}

int main(int argc, const char *argv[])
{
	if (argc != 3)
	{
		printf("Please enter 2 port addresses \n");
		return 0;
	}

	int server_port = atoi(argv[1]), client_port = atoi(argv[2]);
	printf("%d %d\n", server_port, client_port);
	first = server_port;

	int server_fd, new_socket, opt = 1;
	struct sockaddr_in address;
	int addrlen = sizeof(address);
	int first_port = 0;
	unsigned long total_size;
	long uffd;
	char buffer[BUFF_SIZE] = { 0 };
	char temp_buffer[50];
	char cmd;
	pthread_t thread_id, thread_socket;
	struct uffdio_api uffdio_api;
	struct uffdio_register uffdio_register;

	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("socket failed");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
	{
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(server_port);

	if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0)
	{
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	int sock = 0;
	struct sockaddr_in serv_addr;

	if (listen(server_fd, 2) < 0)
	{
		perror("listen");
		exit(EXIT_FAILURE);
	}

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("\n Socket creation error \n");
		return -1;
	}

	memset(&serv_addr, '0', sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(client_port);

	if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
	{
		printf("\nInvalid address/ Address not supported \n");
		return -1;
	}

	printf("Waiting for client at port %d\n", client_port);
	while (connect(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0)
	{
		first_port = server_port;
	}

	printf("Client available on port %d\n", client_port);

	if ((new_socket = accept(server_fd, (struct sockaddr *) &address,
			(socklen_t*) &addrlen)) < 0)
	{
		perror("accept");
		exit(EXIT_FAILURE);
	}

	if (server_port == first_port)
	{
		printf("How many pages would you like to allocate(greater than 0)?\n");
		if (scanf("%d", &pages) > 0)
		{
			printf("%d pages will be allocated\n", pages);
		}

		page_size = sysconf(_SC_PAGE_SIZE);
		total_size = pages * page_size;

		addr = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (addr == MAP_FAILED)
		{
			printf("mmap failed\n");
			return -1;
		}

		printf("The addresses of mmaped region at port %d is %p\n", server_port, addr);
		printf("Size of memory region mapped in process 1 is %lu\n", total_size);
		sprintf(temp_buffer, "%ld", total_size);
		unsigned long temp = (unsigned long) addr;
		send(sock, temp_buffer, TEMP_BUFF_SIZE, 0);
		memset(temp_buffer, 0, TEMP_BUFF_SIZE);
		sprintf(temp_buffer, "%lu", temp);
		send(sock, temp_buffer, TEMP_BUFF_SIZE, 0);
	}
	else
	{
		printf("Press key to receive the data from process 1\n");
		getchar();
		unsigned long mem_addr;
		if (read(new_socket, buffer, TEMP_BUFF_SIZE) < 0)
		{
			printf("\nRead Failed \n");
			return -1;
		}

		total_size = strtoul(buffer, NULL, 10);
		printf("The size of memory mapped from process 1 is %lu\n", total_size);
		if (read(new_socket, buffer, TEMP_BUFF_SIZE) < 0)
		{
			printf("\nRead Failed \n");
			return -1;
		}

		mem_addr = strtoul(buffer, NULL, 10);
		printf("Address of memory mapped in process 1 is %p\n", (void*) mem_addr);
		addr = mmap((void*) mem_addr, total_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (addr == MAP_FAILED)
		{
			printf("mmap failed\n");
			return -1;
		}

		printf("The address of mmaped region at port %d is %p\n", server_port, addr);
		printf("Size of memory region mapped at port %d is %lu\n", server_port, total_size);
		page_size = sysconf(_SC_PAGE_SIZE);
		pages = total_size / page_size;
	}

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd == -1)
		errExit("userfaultfd");

	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1)
		errExit("ioctl-UFFDIO_API");

	// Uffdio register memory region
	uffdio_register.range.start = (unsigned long) addr;
	uffdio_register.range.len = total_size;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;

	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1)
	{
		errExit("ioctl-UFFDIO_REGISTER");
	}

	int pthr_return = pthread_create(&thread_id, NULL, handle_fault, (void*) uffd);
	if (pthr_return != 0)
	{
		errno = pthr_return;
		errExit("pthread_create");
	}

	if (first_port == server_port)
	{
		main_sock = sock;
	}
	else
	{
		main_sock = new_socket;
	}

	int pthr_sock_return = pthread_create(&thread_socket, NULL, check_page, &main_sock);
	if (pthr_sock_return != 0)
	{
		errno = pthr_sock_return;
		errExit("pthread_create");
	}

	msi_array = (char*) calloc(pages, sizeof(char));
	memset(msi_array, 'I', pages* sizeof(msi_array[0]));

	for (;;)
	{
		int chosen_page;
		printf("Which command should I run? (r:read, w:write, v:view msi array):\n");
		scanf(" %c", &cmd);
		printf("For which page? (0-%i, or -1 for all:)\n", pages - 1);
		scanf("%d", &chosen_page);
		if (cmd == 'v')
		{
			view_msi_array(chosen_page);
		}
		else if (cmd == 'r')
		{
			if (chosen_page == -1)
			{
				for (int i = 0; i < pages; i++)
				{
					read_handler(i, total_size);
				}
			}
			else
			{
				read_handler(chosen_page, total_size);
			}
		}
		else
		{
			char input[page_size];
			printf("Type your new message:\n");
			getchar();
			if (fgets(input, page_size, stdin) == NULL)
				errExit("Please enter a messge\n");

			if (chosen_page == -1)
			{
				for (int i = 0; i < pages; i++)
				{
					write_handler(i, input);
				}
			}
			else
			{
				write_handler(chosen_page, input);
			}
		}
	}
	return 0;
}