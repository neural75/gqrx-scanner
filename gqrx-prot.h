/*
MIT License

Copyright (c) 2017 neural75

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
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
bool SetSquelchLevel(int sockfd, double dBFS);
bool GetSignalLevelEx(int sockfd, double *dBFS, int n_samp);



#endif /* _GQRX_PROT_H_ */
