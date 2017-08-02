#ifndef _GQRX_PROT_H_
#define _GQRX_PROT_H_

#define BUFSIZE         1024
#define FREQ_MAX        4096
#define SAVED_FREQ_MAX  1000
#define TAG_MAX         100

typedef unsigned long long freq_t;


// 
// error - wrapper for perror
//
void error(char *msg);

//
// Connect
//
int Connect (char *hostname, int portno);

//
// Send
//
bool Send(int sockfd, char *buf);

//
// Recv
//
bool Recv(int sockfd, char *buf);

//
// GQRX Protocol
//
bool GetCurrentFreq(int sockfd, freq_t *freq);
bool SetFreq(int sockfd, freq_t freq);
bool GetSignalLevel(int sockfd, double *dBFS);
bool GetSquelchLevel(int sockfd, double *dBFS);
bool GetSignalLevelEx(int sockfd, double *dBFS, int n_samp);



#endif /* _GQRX_PROT_H_ */