#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>

#define PORT		"5678"
#define BACKLOG		100
#define MAX_BUF_SZ	100
#define MAX_SERV	10

#define INT_SZ		(sizeof(uint32_t) * 2)
#define D_SZ		(sizeof(double) * 2)
#define LENGTH		(MAX_BUF_SZ + INT_SZ*2 + D_SZ*2)

#define pack754_32(f) (pack754((f), 32, 8))
#define pack754_64(f) (pack754((f), 64, 11))
#define unpack754_32(i) (unpack754((i), 32, 8))
#define unpack754_64(i) (unpack754((i), 64, 11))

#ifndef N_DEBUG
#define DBG(...) do{printf(__VA_ARGS__);printf("\n");}while(0)
#else
#define DBG(...)
#endif

long unsigned int count[MAX_SERV];

typedef struct 
{
	int resp_fd;
	int idx;
	char addrstr[INET6_ADDRSTRLEN];
} handler_arg;

typedef struct 
{
	char symbol[MAX_BUF_SZ];
	uint32_t bidsize;
	uint32_t asksize;
	double bidprice;
	double askprice;
} tuple;

struct _record
{
	tuple val;
	struct _record *next;
};

typedef struct _record record;

void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in *)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

void list_init(record **list)
{
	*list = NULL;

	return;
}

record *add_to_first(record *list, tuple *pkt)
{
	record *new_rec = (record *)malloc(sizeof(record));
	new_rec->val = *pkt;
	new_rec->next = list;
	
	return new_rec;
}

void null(void) {}

void identify(record *list, tuple *pkt, FILE *rec)
{
	fprintf(rec, "%10s%10u%10.2f%10u%10.2f", 
				pkt->symbol, 
				pkt->bidsize,
				pkt->bidprice,
				pkt->asksize,
				pkt->askprice);
	list = add_to_first(list, pkt);
	fflush(rec);

	return;
}

void process(tuple *pkt, FILE *rec)
{
	double x;
	x = pkt->bidsize*pkt->bidprice - pkt->asksize*pkt->askprice;
	fprintf(rec, "%14.2f\n", x);
	fflush(rec);

	return;
}

void starttimer(struct timeval *start_tv)
{
	gettimeofday(start_tv, NULL);

	return;
}

void endtimer(unsigned long int i, struct timeval *start_tv)
{
	struct timeval end_tv;
	gettimeofday(&end_tv, NULL);

	double sec = end_tv.tv_sec - start_tv->tv_sec;
	double microsec = end_tv.tv_usec - start_tv->tv_usec;
	double diff = sec*1000000 + microsec;

	printf("    #pkt    time\n");
	printf("--------------------\n");
	printf("%8lu%13.4f ms\n", i, diff / 1000.0);
	printf("%13.4f pkt/sec\n", i / (diff/1000000.0));

	return;
}

void arg_init(handler_arg *arg, int resp_fd, int idx, char *addr)
{
	arg->resp_fd = resp_fd;
	arg->idx = idx;
	strcpy(arg->addrstr, addr);

	return;
}

uint64_t pack754(long double f, unsigned bits, unsigned expbits)
{
	long double fnorm;
	int shift;
	long long sign, exp, significand;
	unsigned significandbits = bits - expbits - 1; // -1 for sign bit

	if (f == 0.0) return 0; // get this special case out of the way

	// check sign and begin normalization
	if (f < 0) { sign = 1; fnorm = -f; }
	else { sign = 0; fnorm = f; }

	// get the normalized form of f and track the exponent
	shift = 0;
	while(fnorm >= 2.0) { fnorm /= 2.0; shift++; }
	while(fnorm < 1.0) { fnorm *= 2.0; shift--; }
	fnorm = fnorm - 1.0;

	// calculate the binary form (non-float) of the significand data
	significand = fnorm * ((1LL<<significandbits) + 0.5f);

	// get the biased exponent
	exp = shift + ((1<<(expbits-1)) - 1); // shift + bias

	// return the final answer
	return (sign<<(bits-1)) | (exp<<(bits-expbits-1)) | significand;
}

long double unpack754(uint64_t i, unsigned bits, unsigned expbits)
{
	long double result;
	long long shift;
	unsigned bias;
	unsigned significandbits = bits - expbits - 1; // -1 for sign bit

	if (i == 0) return 0.0;

	// pull the significand
	result = (i&((1LL<<significandbits)-1)); // mask
	result /= (1LL<<significandbits); // convert back to float
	result += 1.0f; // add the one back on

	// deal with the exponent
	bias = (1<<(expbits-1)) - 1;
	shift = ((i>>significandbits)&((1LL<<expbits)-1)) - bias;
	while(shift > 0) { result *= 2.0; shift--; }
	while(shift < 0) { result /= 2.0; shift++; }

	// sign it
	result *= (i>>(bits-1))&1? -1.0: 1.0;

	return result;
}

#if 0
long double unpack754(uint64_t i, unsigned int bits, unsigned int expbits)
{
	long double result;
	long long int shift;
	unsigned int bias;
	unsigned significandbits = bits - expbits - 1;

	if (i == 0)	return 0.0;

	result = (1&((1LL<<significandbits)-1));
	result /= (1LL<<significandbits);
	result += 1.0f;

	bias = (1<<(expbits-1)) - 1;
	shift = ((i>>significandbits)&((1LL<<expbits)-1)) - bias;

	while (shift > 0) { result *= 2.0; shift--; }
	while (shift < 0) { result /= 2.0; shift++; }

	result *= (i>>(bits-1))&1? -1.0 : 1.0;

	return result;
}
#endif

void unflatten(char *str, tuple *pkt)
{
	int i;
	uint64_t di0 = 0;
	uint64_t di1 = 0;
	char bidsize[INT_SZ + 1], asksize[INT_SZ + 1];
	char bidprice[D_SZ + 1], askprice[D_SZ + 1];

	for (i = 0; i < MAX_BUF_SZ; i++)	
		pkt->symbol[i] = str[i];
	for (i = 0; i < INT_SZ; i++) {
		bidsize[i] = str[MAX_BUF_SZ+i];
		asksize[i] = str[MAX_BUF_SZ+INT_SZ+i];
	}
	bidsize[i] = '\0';
	asksize[i] = '\0';
	for (i = 0; i < D_SZ; i++) {
		bidprice[i] = str[MAX_BUF_SZ+INT_SZ*2+i];
		askprice[i] = str[MAX_BUF_SZ+INT_SZ*2+D_SZ+i];
	}
	bidprice[i] = '\0';
	askprice[i] = '\0';
	sscanf(bidsize, "%x", &(pkt->bidsize));
	sscanf(asksize, "%x", &(pkt->asksize));
	sscanf(bidprice, "%lx", &di0);
	sscanf(askprice, "%lx", &di1);

	pkt->bidprice = unpack754_64(di0);
	pkt->askprice = unpack754_64(di1);

	return;
}

int recv_helper(int resp_fd, char *str, size_t len)
{
	size_t total = 0;
	size_t bytesleft = len;
	int n = -2;

	while (total < len) {
		n = recv(resp_fd, str+total, bytesleft, 0);
		if (n == -1 || n == 0)	break;
		total += n;
		bytesleft -= n;
	}

	return (n == -1) ? -1 : n;
}

void *req_handler(void *argument)
{
	handler_arg *arg = argument;
	char text[LENGTH];
	int resp_fd = arg->resp_fd;
	int i = arg->idx;
	tuple recvpkt;
	record *rec_list;
	list_init(&rec_list);

	char *filename = (char *)malloc(MAX_BUF_SZ*sizeof(char));
	sprintf(filename, "%s", arg->addrstr);
	strcat(filename, "_record");
	FILE *fs = fopen(filename, "w+");
	struct timeval start_tv;

	int status;
	starttimer(&start_tv);
	while (1) {	
#if 0
		if ((status = recv(resp_fd, text, sizeof(text), 0)) == -1) {
			perror("recv");
			close(resp_fd);
			exit(0);
		} else if (status == 0)	break;
		else if (status < sizeof(tuple)) {	DBG("not recv all..."); continue; }
#endif
		if ((status = recv_helper(resp_fd, text, sizeof(text))) == -1) {
			perror("recv_all");
			close(resp_fd);
			exit(0);
		} else if (status == 0) break;

//		if (strcmp("CS@VT", text))	{ DBG("wrong!..."); continue; }
		unflatten(text, &recvpkt);
	
		null();
		identify(rec_list, &recvpkt, fs);
		process(&recvpkt, fs);

		count[i]++;
	}
	endtimer(count[i], &start_tv);
	close(resp_fd);
	free(rec_list);
	fclose(fs);

	return NULL;
}

void *stopwatch(void *argument)
{
	FILE *log = argument;
	struct timeval stop_tv;
	long unsigned int msgcount = 0;
	int i;
	while (1) {
		sleep(3);
		for (i = 0; i < MAX_SERV; i++)	msgcount += count[i];
		gettimeofday(&stop_tv, NULL);
		fprintf(log, "%15lu%29.4f ms\n", msgcount, 
					(double)((stop_tv.tv_sec*1000000 + stop_tv.tv_usec) / 1000.0));
		fflush(log);
		printf("%15lu%29.4f ms\n", msgcount, 
					(double)((stop_tv.tv_sec*1000000 + stop_tv.tv_usec) / 1000.0));
		msgcount = 0;
	}

	return NULL;
}

int main(void) 
{
	int ret;
	int sockfd;
	int yes = 1;
	socklen_t socksz;
	char addrstr[INET6_ADDRSTRLEN];
	struct addrinfo hints;
	struct addrinfo *servinfo;
	struct addrinfo *tmp;
	struct sockaddr_storage clientaddr;

//	struct timeval start_tv;
	pthread_t tid_daemon, tid_req;
	FILE *log = fopen("log", "w+");

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if ((ret = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
		return 1;
	}

	for (tmp = servinfo; tmp != NULL; tmp = tmp->ai_next) {
		if ((sockfd = socket(tmp->ai_family, tmp->ai_socktype, tmp->ai_protocol))
					== -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))
					== -1) {
			perror("server: setsockopt");
			exit(1);
		}

		if (bind(sockfd, tmp->ai_addr, tmp->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (tmp == NULL) {
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo);

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	DBG("======== server: waiting for connections...");

	handler_arg arg[MAX_SERV];
	memset(&arg, 0, sizeof(arg));
	int i = 0;

	for (i; i < MAX_SERV; i++)	count[i] = 0;
	i = 0;
	if (pthread_create(&tid_daemon, NULL, stopwatch, (void *)log) != 0) {
		perror("pthread_create");
		exit(1);
	}
	
	while (1) {
		socksz = sizeof(clientaddr);
		int resp_fd;
		if ((resp_fd = accept(sockfd, (struct sockaddr *)&clientaddr, &socksz)) ==
					-1) {
			perror("accept");
			continue;
		}
		inet_ntop(clientaddr.ss_family, 
					get_in_addr((struct sockaddr *)&clientaddr),
					addrstr,
					sizeof(addrstr));
		printf("server: got connection from %s\n", addrstr);
		
		arg_init(&arg[i], resp_fd,  i, addrstr);
		if (pthread_create(&tid_req, NULL, req_handler, (void *)&arg[i])
					!= 0) {
			perror("pthread_create");
			exit(1);
		}
		i++;
	}

//	stopwatch(log);
	close(sockfd);
	fclose(log);

	return 0;
}
