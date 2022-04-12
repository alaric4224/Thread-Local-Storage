#include "tls.h"
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <search.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>


#define MAX_THREADS 128

//cc -Wall -Werror -std=gnu99 -pedantic -O0 -g -pthread -o clone tls.o test_clone_many.c

/*
 * This is a good place to define any data structures you will use in this file.
 * For example:
 *  - struct TLS: may indicate information about a thread's local storage
 *    (which thread, how much storage, where is the storage in memory)
 *  - struct page: May indicate a shareable unit of memory (we specified in
 *    homework prompt that you don't need to offer fine-grain cloning and CoW,
 *    and that page granularity is sufficient). Relevant information for sharing
 *    could be: where is the shared page's data, and how many threads are sharing it
 *  - Some kind of data structure to help find a TLS, searching by thread ID.
 *    E.g., a list of thread IDs and their related TLS structs, or a hash table.
 */

struct page {

    pthread_t owner;
    
    void * loc;
    
    unsigned int numshare;
    
};


struct TLS {
    
    pthread_t owner; 
    
    unsigned int pagesize;
    
    unsigned int numpages;
    
    struct page ** list;
    
};

/*
 * Now that data structures are defined, here's a good place to declare any
 * global variables.
 */

struct TLS * tls[MAX_THREADS];

pthread_mutex_t * mutexes[MAX_THREADS];

unsigned int psize;

unsigned int tlscount;

struct sigaction act;

/*
 * With global data declared, this is a good point to start defining your
 * static helper functions.
 */
void segHandler(int sig, siginfo_t * info, void * ucontext){
    
    long long unsigned int fault = (long long unsigned int) (((long long unsigned int) info->si_addr) & ~(getpagesize() - 1));
    //long long unsigned int fault = (long long unsigned int) (((long long unsigned int)info->si_addr >> 12) << 12);
    
    for(int i = 0; i < MAX_THREADS; i++){
        if(tls[i] == NULL){continue;}
        for(int j = 0; j < tls[i]->numpages; j++){
            if(((long long unsigned int) tls[i]->list[j]->loc) == fault){
                pthread_exit(NULL);
                return;
            }
        }
    }
    
    signal(SIGBUS, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    raise(sig);
}

void tlsSetup(){
    
    psize = getpagesize();
    
    for(int i = 0; i < MAX_THREADS; i++){
        tls[i] = NULL;
        mutexes[i] = malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(mutexes[i], NULL);
    }
    
    tlscount = 0;
    
    act.sa_sigaction = segHandler;
    
    act.sa_flags = SA_SIGINFO;
    
    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGBUS, &act, NULL);
    
}

int findtls(pthread_t tnum){
    int found = -1;
    int i;
    for(i = 0; i < MAX_THREADS; i++){
        if(tls[i] == NULL){}
        else if(tls[i]->owner == tnum){found = i; return found;}
    }
    return found;
}
/*
 * Lastly, here is a good place to add your externally-callable functions.
 */ 

int tls_create(unsigned int size)
{
    static int firstCall = 1;
    if(firstCall){
        tlsSetup();
        firstCall = 0;
    }
  
    if(findtls(pthread_self()) != -1){
        return -1;
    }
    
    int index = tlscount++;
    int i;
    
    if(index == -1){return -1;}
    
    tls[index] = (struct TLS*) malloc(sizeof(struct TLS));
    
    tls[index]->owner = pthread_self();
    
    tls[index]->pagesize = size; 
    
    tls[index]->numpages = (unsigned int) size/getpagesize();
    
    if(size%getpagesize() > 0){tls[index]->numpages++;}
    
    tls[index]->list = malloc(tls[index]->numpages*sizeof(struct tlsList*));
    
    for(i = 0; i < tls[index]->numpages; i++){
        tls[index]->list[i] = malloc(sizeof(struct page));
        tls[index]->list[i]->owner = pthread_self();
        tls[index]->list[i]->numshare = 1;
        tls[index]->list[i]->loc = mmap(NULL, getpagesize(), PROT_NONE, MAP_SHARED|MAP_ANON, -1, 0); //MAP PRIVATE?
    }
    
	return 0;
}

int tls_destroy() //NEED TO FIX: SEE PIAZZA POST YOU IDIOT
{
    int index = findtls(pthread_self());
    if(index == -1){return -1;}
    
    for(int i = 0; i < tls[index]->numpages; i++){
        if(tls[index]->list[i]->numshare > 1){
            tls[index]->list[i]->numshare--;
        }
        else{
            munmap(tls[index]->list[i]->loc, getpagesize());
            free(tls[index]->list[i]);
        }
    }
    free(tls[index]->list);
    free(tls[index]);
    tls[index] = NULL;
	return 0;
}

int tls_read(unsigned int offset, unsigned int length, char *buffer)
{
    int index = findtls(pthread_self());
    if(index == -1){return -1;}
    if(offset + length > tls[index]->pagesize){return -1;}
    unsigned int set = offset;
    unsigned int counter = 0;
    int i = 0; //page index
    int j = 0; //counter index
    while(set >= getpagesize()){
        i++;
        set -= getpagesize();
    }
    for(i = i; i < tls[index]->numpages; i++){
        char* d = (char *) tls[index]->list[i]->loc;
        mprotect(d, psize, PROT_READ|PROT_WRITE);
        j = 0;
        if(set > 0){
            j = set;
            set = 0;
        }
        while(j < getpagesize()){
            if(counter >= length){mprotect(d, psize, PROT_NONE); return 0;}
            buffer[counter] = d[j];
            counter++;
            j++;
        }
        mprotect(d, psize, PROT_NONE);
    }
	return 0;
}

int tls_write(unsigned int offset, unsigned int length, const char *buffer)
{
    int index = findtls(pthread_self());
    if(index == -1){return -1;}
    if(offset + length > tls[index]->pagesize){return -1;}
    unsigned int set = offset;
    unsigned int counter = 0;
    int i = 0; //page index
    int j = 0; //counter index
    while(set >= getpagesize()){
        i++;
        set -= getpagesize();
    }
    for(i = i; i < tls[index]->numpages; i++){
        if(tls[index]->list[i]->numshare > 1){
            struct page* newpage = malloc(sizeof(struct page));
            newpage->owner = pthread_self();
            newpage->numshare = 1;
            tls[index]->list[i]->numshare--;
            newpage->loc = mmap(NULL, getpagesize(), PROT_NONE, MAP_SHARED|MAP_ANON, -1, 0);
            mprotect(tls[index]->list[i]->loc, psize, PROT_READ|PROT_WRITE);
            mprotect(newpage->loc, psize, PROT_READ|PROT_WRITE);
            char * c = (char *) newpage->loc;
            char * e = (char *) tls[index]->list[i]->loc;
            for(int k = 0; k < getpagesize(); k++){    
                c[k] = e[k];
            }
            mprotect(tls[index]->list[i]->loc, psize, PROT_NONE);
            mprotect(newpage, psize, PROT_NONE);
            tls[index]->list[i] = newpage;
        }
        char* d = (char *) tls[index]->list[i]->loc;
        mprotect(d, psize, PROT_READ|PROT_WRITE);
        j = 0;
        if(set > 0){
            j = set;
            set = 0;
        }
        while(j < getpagesize()){
            if(counter >= length){mprotect(d, psize, PROT_NONE); return 0;}
            d[j] = buffer[counter];
            counter++;
            j++;
        }
        mprotect(d, psize, PROT_NONE);
    }
	return 0;
}

int tls_clone(pthread_t tid)
{
    if(findtls(pthread_self()) != -1){return -1;}
    int olddex = findtls(tid);
    if(olddex < 0){return -1;}
    pthread_mutex_lock(mutexes[olddex]);
    int index = tlscount++;
    
    if(index < 0){return -1;}
    
    tls[index] = (struct TLS*) malloc(sizeof(struct TLS));
    
    tls[index]->owner = pthread_self();
    
    tls[index]->pagesize = tls[olddex]->pagesize;
    
    tls[index]->numpages = tls[olddex]->numpages;
    
    tls[index]->list = malloc(tls[index]->numpages*sizeof(struct tlsList*));
    
    for(int i = 0; i < tls[olddex]->numpages; i++){
        tls[index]->list[i] = tls[olddex]->list[i];
        tls[index]->list[i]->numshare++;
    }
    pthread_mutex_unlock(mutexes[olddex]);
	return 0;
}
