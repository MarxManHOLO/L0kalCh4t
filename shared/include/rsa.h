
#ifndef _RSA_H_
#define _RSA_H_



typedef struct
{
  unsigned b; 
  char *d; 
  char *e; 
} __rsa_key_struct;

typedef __rsa_key_struct rsa_key_t[1];




int rsa_init(rsa_key_t pub, rsa_key_t priv, const unsigned keylen, const unsigned base);



void rsa_clear_key(rsa_key_t key);



int rsa_encrypt(char* enc, const unsigned count, const char* raw, const rsa_key_t pub);



int rsa_decrypt(char* raw, const unsigned count, const char* enc, const rsa_key_t priv);



unsigned rsa_max_bytes(const rsa_key_t key);


#endif

